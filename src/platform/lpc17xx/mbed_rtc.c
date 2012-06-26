// MBED specific RTC support
#include <string.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "platform_conf.h"
#include "auxmods.h"
#include "LPC17xx.h"
#include "mbed_rtc.h"
#include <time.h>

// ****************************************************************************
// Helpers and local variables

static const char *mbed_datetime_names[] = { "day", "month", "year", "hour", "min", "sec" };
static const uint16_t mbed_datetime_max[] = { 31,   12,      9999,   24,     60,    60};
static const uint16_t mbed_datetime_min[] = { 1,    1,       0,      0,      0,     0};
static const void * mbed_datetime_regs[] = { &(LPC_RTC->DOM), &(LPC_RTC->MONTH), &(LPC_RTC->YEAR), 
                                             &(LPC_RTC->HOUR), &(LPC_RTC->MIN), &(LPC_RTC->SEC)}; 

// ****************************************************************************

// C LIB

void platform_rtc_get( int* day, int* month, int* year, int* hour, int* min, int* sec  )
{
  *year = LPC_RTC->YEAR;
  *month = LPC_RTC->MONTH;
  *day = LPC_RTC->DOM;

  *hour = LPC_RTC->HOUR;
  *min = LPC_RTC->MIN;
  *sec = LPC_RTC->SEC;
}

void platform_rtc_set( int day, int month, int year, int hour, int min, int sec )
{
  // RTC OFF
//  LPC_RTC->CCR = 2; // Reset
  LPC_RTC->CCR = 0;

  // Set datetime
  LPC_RTC->HOUR = hour; 
  LPC_RTC->MIN = min;
  LPC_RTC->SEC = sec;
  
  LPC_RTC->YEAR = year;
  LPC_RTC->MONTH = month;
  LPC_RTC->DOM = day;

  // RTC ON, Calibration OFF
  LPC_RTC->CCR = 1 | 1<<4; // Clock enabled, calibration disabled
}

void platform_rtc_setalarm( int day, int month, int year, int hour, int min, int sec )
{
  // RTC OFF
//  LPC_RTC->CCR = 0;
//  NVIC_DisableIRQ(RTC_IRQn);

  // Set datetime
  LPC_RTC->ALYEAR = year;
  LPC_RTC->ALMON = month;
  LPC_RTC->ALDOM = day;

  LPC_RTC->ALHOUR = hour; 
  LPC_RTC->ALMIN = min;
  LPC_RTC->ALSEC = sec;

  // Set mask ( Ignore DOY and DOW )
  LPC_RTC->AMR = 1<<4 | 1<<5;
  
  // TMP - Enable alarm interrupt
  NVIC_ClearPendingIRQ(RTC_IRQn);
  NVIC_SetPriority(RTC_IRQn, ((0x01<<3)|0x01)); // <- important!
//  NVIC_EnableIRQ(RTC_IRQn);

  //Clear interrupt flags for both clock and alarms.
  LPC_RTC->ILR |= (1<<0)|(1<<1);

  // RTC ON, Calibration OFF
//  LPC_RTC->CCR = 1 | 1<<4; // Clock enabled, calibration disabled
}

// ****************************************************************************

// LUA Lib

// Lua: mbed.rtc.set( arg )
// arg can be a string formated as dd/mm/yyyy hh:mm:ss
// or a table with 'day', 'month', 'year', 'hour', 'minute' and 'second' fields
static int mbed_rtc_set( lua_State *L )
{
  const char *data;
  int vals[6] = {-1, -1, -1, -1, -1, -1}; 
  unsigned i;
  int sz;
  char error[20] = "invalid ";

  // If we receive a string, split the time using sscanf
  if( lua_isstring( L, 1 ) )
  {
    data = luaL_checkstring( L, 1 );

    if( sscanf( data, "%d/%d/%d %d:%d:%d%n", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5], &sz ) != 6 || sz != strlen( data ) )
      return luaL_error( L, "invalid datetime format" );

    platform_rtc_set( vals[0], vals[1], vals[2], vals[3], vals[4], vals[5] );
  }
  else // If we receive a table, get the values by their name
  {
    luaL_checktype( L, 1, LUA_TTABLE );
    for( i = 0; i < 6; i ++ )
    {
      lua_getfield( L, 1, mbed_datetime_names[i] );

      if ( lua_type( L, -1 ) == LUA_TNUMBER )
      {
        vals[i] = luaL_checkinteger( L, -1 );

        // Test the range and set the register
        if (vals[i] < mbed_datetime_min[i] || vals[i] > mbed_datetime_max[i])
        {
          strcat(error, mbed_datetime_names[i]);
          luaL_error( L, error );
        }
        else
          if (i == 2) // Year is the only 16bit val
            *((uint16_t *)mbed_datetime_regs[i]) = vals[i];
          else
            *((uint8_t *)mbed_datetime_regs[i]) = vals[i];
      }

      lua_pop( L, 1 );
    }
  }

  return 0;
}

// Lua: time = mbed.rtc.get( format )
// format can be '*s' to return the datetime as a string dd/mm/yyyy hh:mm:ss
// or '*t' to return the time as a table with fields 'day', 'month', 'year', 'hour', 'minute' and 'second'
static int mbed_rtc_get( lua_State *L )
{
  int dd = -1, mon = -1, yy = -1, hh = -1, mm = -1, ss = -1;
  int *pvals[] = { &dd, &mon, &yy, &hh, &mm, &ss };
  const char *fmt = luaL_checkstring( L, 1 );
  char buff[ 20 ];
  unsigned i;
  
  platform_rtc_get( &dd, &mon, &yy, &hh, &mm, &ss );

  if( !strcmp( fmt, "*s" ) )
  {
    sprintf( buff, "%02d/%02d/%04d %02d:%02d:%02d", dd, mon, yy, hh, mm, ss );
    lua_pushstring( L, buff );
  }
  else if( !strcmp( fmt, "*t" ) )
  {
    lua_newtable( L );
    for( i = 0; i < 6; i ++ )
    {
      lua_pushstring( L, mbed_datetime_names[ i ] );
      lua_pushinteger( L, *pvals[ i ] );
      lua_settable( L, -3 );
    }
  }
  else
    return luaL_error( L, "invalid format" );
  return 1;
}

// Lua: mbed.rtc.setalarmdate( arg )
// arg can be a string formated as dd/mm/yyyy hh:mm:ss
// or a table with 'day', 'month', 'year', 'hour', 'minute' and 'second' fields
static int mbed_rtc_setalarm( lua_State *L )
{
  const char *data;
  int dd = -1, mon = -1, yy = -1, hh = -1, mm = -1, ss = -1;
  int *pvals[] = { &dd, &mon, &yy, &hh, &mm, &ss };
  unsigned i;
  int sz;

  // If we receive a string, split the time using sscanf
  if( lua_isstring( L, 1 ) )
  {
    data = luaL_checkstring( L, 1 );      
    if( sscanf( data, "%d/%d/%d %d:%d:%d%n", &dd, &mon, &yy, &hh, &mm, &ss, &sz ) != 6 || sz != strlen( data ) )
      return luaL_error( L, "invalid datetime format" );
  }
  else // If we receive a table, get the values by their name
  {
    luaL_checktype( L, 1, LUA_TTABLE );
    for( i = 0; i < 6; i ++ )
    {
      lua_pushstring( L, mbed_datetime_names[ i ] );
      lua_gettable( L, -2 );
      *pvals[ i ] = luaL_checkinteger( L, -1 );
      lua_pop( L, 1 ); 
    }
  }

  // Check if the time is valid
  if( hh < 0 || hh >= 24 )
    return luaL_error( L, "invalid hour" );
  if( mm < 0 || mm >= 60 )
    return luaL_error( L, "invalid minute" );
  if( ss < 0 || ss >= 60 )
    return luaL_error( L, "invalid second" );
  if( dd < 1 || dd > 31 )
    return luaL_error( L, "invalid day" );
  if( mon < 1 || mon > 12 )
    return luaL_error( L, "invalid month" );
  if( yy < 0 || yy > 9999 )
    return luaL_error( L, "invalid year" );    

  platform_rtc_setalarm( dd, mon, yy, hh, mm, ss ); 

  return 0;
}

static int mbed_rtc_alarmed( lua_State *L )
{
  lua_pushboolean( L, (LPC_RTC->ILR & 2) >> 1 ); 
  LPC_RTC->ILR |= 2; // Clear alarm flag
  return 1;
}

static int mbed_rtc_strftime( lua_State *L )
{
  char out[51];
  struct tm t;
  t.tm_sec = 1;
  t.tm_min = 2;
  t.tm_hour = 3;
  t.tm_mday = 1;
  t.tm_mon = 1;
  t.tm_year = 2000 - 1900;
  t.tm_wday = 6;
  t.tm_yday = 0;
  t.tm_isdst = 0;

  strftime( out, 50, "%c", &t );

  lua_pushstring( L, out );

  return 1;
}

// Module function map
#define MIN_OPT_LEVEL 2
#include "lrodefs.h" 
const LUA_REG_TYPE mbed_rtc_map[] =
{
  { LSTRKEY( "set" ),  LFUNCVAL( mbed_rtc_set ) },
  { LSTRKEY( "get" ),  LFUNCVAL( mbed_rtc_get ) },
  { LSTRKEY( "setalarm" ),  LFUNCVAL( mbed_rtc_setalarm ) },
  { LSTRKEY( "alarmed" ),  LFUNCVAL( mbed_rtc_alarmed ) },
  { LSTRKEY( "strftime" ),  LFUNCVAL( mbed_rtc_strftime ) },
  { LNILKEY, LNILVAL }
};
