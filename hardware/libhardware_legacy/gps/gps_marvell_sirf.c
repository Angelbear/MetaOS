
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define  LOG_TAG  "gps_mrvl"

#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware_legacy/gps.h>
#include <hardware_legacy/gps_ni.h>

#define  GPS_DEBUG  1

#define  D_KEY(...)     LOGD(__VA_ARGS__)

#if GPS_DEBUG == 2
#define  D(...)         LOGD(__VA_ARGS__)
#define  D_LOW(...)     LOGD(__VA_ARGS__)

#elif GPS_DEBUG == 1
#define  D(...)         LOGD(__VA_ARGS__)
#define  D_LOW(...)     ((void)0)

#else
#define  D(...)         ((void)0)
#define  D_LOW(...)     ((void)0)
#endif

#undef TIMER_THREAD

#define SIRF_LIB_PATH "/system/lib/liblsm_gsd4t.so"

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   T O K E N I Z E R                     *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

typedef struct {
    const char*  p;
    const char*  end;
} Token;

#define  MAX_NMEA_TOKENS  32

typedef struct {
    int     count;
    Token   tokens[ MAX_NMEA_TOKENS ];
} NmeaTokenizer;

static int
nmea_tokenizer_init( NmeaTokenizer*  t, const char*  p, const char*  end )
{
    int    count = 0;
    char*  q;

    // the initial '$' is optional
    if (p < end && p[0] == '$')
        p += 1;

    // remove trailing newline
    if (end > p && end[-1] == '\n') {
        end -= 1;
        if (end > p && end[-1] == '\r')
            end -= 1;
    }

    // get rid of checksum at the end of the sentecne
    if (end >= p+3 && end[-3] == '*') {
        end -= 3;
    }

    while (p < end) {
        const char*  q = p;

        q = memchr(p, ',', end-p);
        if (q == NULL)
            q = end;

        if (count < MAX_NMEA_TOKENS) {
            t->tokens[count].p   = p;
            t->tokens[count].end = q;
            count += 1;
        }

        if (q < end)
            q += 1;

        p = q;
    }

    t->count = count;
    return count;
}

static Token
nmea_tokenizer_get( NmeaTokenizer*  t, int  index )
{
    Token  tok;
    static const char*  dummy = "";

    if (index < 0 || index >= t->count) {
        tok.p = tok.end = dummy;
    } else
        tok = t->tokens[index];

    return tok;
}


static int
str2int( const char*  p, const char*  end )
{
    int   result = 0;
    int   len    = end - p;

    if (len == 0) {
      return -1;
    }

    for ( ; len > 0; len--, p++ )
    {
        int  c;

        if (p >= end)
            goto Fail;

        c = *p - '0';
        if ((unsigned)c >= 10)
            goto Fail;

        result = result*10 + c;
    }
    return  result;

Fail:
    return -1;
}

static double
str2float( const char*  p, const char*  end )
{
    int   result = 0;
    int   len    = end - p;
    char  temp[16];

    if (len == 0) {
      return -1.0;
    }

    if (len >= (int)sizeof(temp))
        return 0.;

    memcpy( temp, p, len );
    temp[len] = 0;
    return strtod( temp, NULL );
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   P A R S E R                           *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

/* Nmea Parser stuff */
#define  NMEA_MAX_SIZE  83

typedef struct {
    int     pos;
    int     overflow;
    int     utc_year;
    int     utc_mon;
    int     utc_day;
    int     utc_diff;
    GpsLocation  fix;
    GpsSvStatus  sv_status;
    int     sv_status_changed;
    char    in[ NMEA_MAX_SIZE+1 ];
} NmeaReader;

static void
nmea_reader_update_utc_diff( NmeaReader*  r )
{
    time_t         now = time(NULL);
    struct tm      tm_local;
    struct tm      tm_utc;
    long           time_local, time_utc;

    gmtime_r( &now, &tm_utc );
    localtime_r( &now, &tm_local );

    time_local = tm_local.tm_sec +
                 60*(tm_local.tm_min +
                 60*(tm_local.tm_hour +
                 24*(tm_local.tm_yday +
                 365*tm_local.tm_year)));

    time_utc = tm_utc.tm_sec +
               60*(tm_utc.tm_min +
               60*(tm_utc.tm_hour +
               24*(tm_utc.tm_yday +
               365*tm_utc.tm_year)));

    r->utc_diff = time_utc - time_local;
}


static void
nmea_reader_init( NmeaReader*  r )
{
    memset( r, 0, sizeof(*r) );

    r->pos      = 0;
    r->overflow = 0;
    r->utc_year = -1;
    r->utc_mon  = -1;
    r->utc_day  = -1;

    nmea_reader_update_utc_diff( r );
}

static int
nmea_reader_update_time( NmeaReader*  r, Token  tok )
{
    int        hour, minute;
    double     seconds;
    struct tm  tm;
    time_t     fix_time;

    if (tok.p + 6 > tok.end)
        return -1;

    if (r->utc_year < 0) {
        // no date yet, get current one
        time_t  now = time(NULL);
        gmtime_r( &now, &tm );
        r->utc_year = tm.tm_year + 1900;
        r->utc_mon  = tm.tm_mon + 1;
        r->utc_day  = tm.tm_mday;
    }

    hour    = str2int(tok.p,   tok.p+2);
    minute  = str2int(tok.p+2, tok.p+4);
    seconds = str2float(tok.p+4, tok.end);

    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = (int) seconds;
    tm.tm_year = r->utc_year - 1900;
    tm.tm_mon  = r->utc_mon - 1;
    tm.tm_mday = r->utc_day;

    fix_time = mktime( &tm ) + r->utc_diff;
    r->fix.timestamp = (long long)fix_time * 1000;
    return 0;
}

static int
nmea_reader_update_cdate( NmeaReader*  r, Token  tok_d, Token tok_m, Token tok_y )
{

    if ( (tok_d.p + 2 > tok_d.end) ||
         (tok_m.p + 2 > tok_m.end) ||
         (tok_y.p + 4 > tok_y.end) )
        return -1;

    r->utc_day = str2int(tok_d.p,   tok_d.p+2);
    r->utc_mon = str2int(tok_m.p, tok_m.p+2);
    r->utc_year = str2int(tok_y.p, tok_y.end+4);

    return 0;
}

static int
nmea_reader_update_date( NmeaReader*  r, Token  date, Token  time )
{
    Token  tok = date;
    int    day, mon, year;

    if (tok.p + 6 != tok.end) {
        D("date not properly formatted: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    day  = str2int(tok.p, tok.p+2);
    mon  = str2int(tok.p+2, tok.p+4);
    year = str2int(tok.p+4, tok.p+6) + 2000;

    if ((day|mon|year) < 0) {
        D("date not properly formatted: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }

    r->utc_year  = year;
    r->utc_mon   = mon;
    r->utc_day   = day;

    return nmea_reader_update_time( r, time );
}


static double
convert_from_hhmm( Token  tok )
{
    double  val     = str2float(tok.p, tok.end);
    int     degrees = (int)(floor(val) / 100);
    double  minutes = val - degrees*100.;
    double  dcoord  = degrees + minutes / 60.0;
    return dcoord;
}


static int
nmea_reader_update_latlong( NmeaReader*  r,
                            Token        latitude,
                            char         latitudeHemi,
                            Token        longitude,
                            char         longitudeHemi )
{
    double   lat, lon;
    Token    tok;

    tok = latitude;
    if (tok.p + 6 > tok.end) {
        D("latitude is too short: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    lat = convert_from_hhmm(tok);
    if (latitudeHemi == 'S')
        lat = -lat;

    tok = longitude;
    if (tok.p + 6 > tok.end) {
        D("longitude is too short: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    lon = convert_from_hhmm(tok);
    if (longitudeHemi == 'W')
        lon = -lon;

    r->fix.flags    |= GPS_LOCATION_HAS_LAT_LONG;
    r->fix.latitude  = lat;
    r->fix.longitude = lon;
    return 0;
}


static int
nmea_reader_update_altitude( NmeaReader*  r,
                             Token        altitude,
                             Token        units )
{
    double  alt;
    Token   tok = altitude;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_ALTITUDE;
    r->fix.altitude = str2float(tok.p, tok.end);
    return 0;
}

static int
nmea_reader_update_accuracy( NmeaReader*  r,
                             Token        accuracy )
{
    double  acc;
    Token   tok = accuracy;

    if (tok.p >= tok.end)
        return -1;

    r->fix.accuracy = str2float(tok.p, tok.end);

    if (r->fix.accuracy == 99.99){
      return 0;
    }

    r->fix.flags   |= GPS_LOCATION_HAS_ACCURACY;
    return 0;
}

static int
nmea_reader_update_bearing( NmeaReader*  r,
                            Token        bearing )
{
    double  alt;
    Token   tok = bearing;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_BEARING;
    r->fix.bearing  = str2float(tok.p, tok.end);
    return 0;
}


static int
nmea_reader_update_speed( NmeaReader*  r,
                          Token        speed )
{
    double  alt;
    Token   tok = speed;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_SPEED;
    r->fix.speed    = str2float(tok.p, tok.end);
    return 0;
}

/* Key entrance function to parse NMEA info */
static void
nmea_reader_parse( NmeaReader*  r )
{
   /* we received a complete sentence, now parse it to generate
    * a new GPS fix...
    */
    NmeaTokenizer  tzer[1];
    Token          tok;

    D("%s: Received: '%.*s'", __FUNCTION__, r->pos, r->in);

    if (r->pos < 9) {
        D("Too short. discarded.");
        return;
    }

    nmea_tokenizer_init(tzer, r->in, r->in + r->pos);
#if GPS_DEBUG == 2
    {
        int  n;
        D("Found %d tokens", tzer->count);
        for (n = 0; n < tzer->count; n++) {
            Token  tok = nmea_tokenizer_get(tzer,n);
            D("%2d: '%.*s'", n, tok.end-tok.p, tok.p);
        }
    }
#endif

    tok = nmea_tokenizer_get(tzer, 0);

    if (tok.p + 5 > tok.end) {
        D("sentence id '%.*s' too short, ignored.", tok.end-tok.p, tok.p);
        return;
    }

    // ignore first two characters.
    tok.p += 2;

    if ( !memcmp(tok.p, "GGA", 3) ) {
        // GPS fix
        Token  tok_fixstaus      = nmea_tokenizer_get(tzer,6);

        if (tok_fixstaus.p[0] > '0') {

          Token  tok_time          = nmea_tokenizer_get(tzer,1);
          Token  tok_latitude      = nmea_tokenizer_get(tzer,2);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,3);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,4);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,5);
          Token  tok_altitude      = nmea_tokenizer_get(tzer,9);
          Token  tok_altitudeUnits = nmea_tokenizer_get(tzer,10);

          nmea_reader_update_time(r, tok_time);
          nmea_reader_update_latlong(r, tok_latitude,
                                        tok_latitudeHemi.p[0],
                                        tok_longitude,
                                        tok_longitudeHemi.p[0]);
          nmea_reader_update_altitude(r, tok_altitude, tok_altitudeUnits);
D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);
        }

D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);

    }

    else if ( !memcmp(tok.p, "GLL", 3) ) {

        Token  tok_fixstaus      = nmea_tokenizer_get(tzer,6);

        if (tok_fixstaus.p[0] == 'A') {

          Token  tok_latitude      = nmea_tokenizer_get(tzer,1);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,2);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,3);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,4);
          Token  tok_time          = nmea_tokenizer_get(tzer,5);

          nmea_reader_update_time(r, tok_time);
          nmea_reader_update_latlong(r, tok_latitude,
                                        tok_latitudeHemi.p[0],
                                        tok_longitude,
                                        tok_longitudeHemi.p[0]);
D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);
        }

D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);

    }

    else if ( !memcmp(tok.p, "GSA", 3) ) {

        Token  tok_fixStatus   = nmea_tokenizer_get(tzer, 2);
        int i;

        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != '1') {

          Token  tok_accuracy      = nmea_tokenizer_get(tzer, 15);

          nmea_reader_update_accuracy(r, tok_accuracy);

          r->sv_status.used_in_fix_mask = 0ul;

          for (i = 3; i <= 14; ++i){

            Token  tok_prn  = nmea_tokenizer_get(tzer, i);
            int prn = str2int(tok_prn.p, tok_prn.end);

            if (prn > 0){
              r->sv_status.used_in_fix_mask |= (1ul << (32 - prn));
              r->sv_status_changed = 1;
              D("%s: fix mask is %d", __FUNCTION__, r->sv_status.used_in_fix_mask);
D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);
            }

          }

        }

D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);

    } else if ( !memcmp(tok.p, "GSV", 3) ) {

        Token  tok_noSatellites  = nmea_tokenizer_get(tzer, 3);
        int    noSatellites = str2int(tok_noSatellites.p, tok_noSatellites.end);

        if (noSatellites > 0) {

          Token  tok_noSentences   = nmea_tokenizer_get(tzer, 1);
          Token  tok_sentence      = nmea_tokenizer_get(tzer, 2);

          int sentence = str2int(tok_sentence.p, tok_sentence.end);
          int totalSentences = str2int(tok_noSentences.p, tok_noSentences.end);
          int curr;
          int i;

          if (sentence == 1) {
              r->sv_status_changed = 0;
              r->sv_status.num_svs = 0;
          }

          curr = r->sv_status.num_svs;

          i = 0;

          while (i < 4 && r->sv_status.num_svs < noSatellites){

                 Token  tok_prn = nmea_tokenizer_get(tzer, i * 4 + 4);
                 Token  tok_elevation = nmea_tokenizer_get(tzer, i * 4 + 5);
                 Token  tok_azimuth = nmea_tokenizer_get(tzer, i * 4 + 6);
                 Token  tok_snr = nmea_tokenizer_get(tzer, i * 4 + 7);

                 r->sv_status.sv_list[curr].prn = str2int(tok_prn.p, tok_prn.end);
                 r->sv_status.sv_list[curr].elevation = str2float(tok_elevation.p, tok_elevation.end);
                 r->sv_status.sv_list[curr].azimuth = str2float(tok_azimuth.p, tok_azimuth.end);
                 r->sv_status.sv_list[curr].snr = str2float(tok_snr.p, tok_snr.end);

                 r->sv_status.num_svs += 1;

                 curr += 1;

                 i += 1;
          }

          if (sentence == totalSentences) {
              r->sv_status_changed = 1;
          }

          D("%s: GSV message with total satellites %d", __FUNCTION__, noSatellites);

        }

    }

    else if ( !memcmp(tok.p, "RMC", 3) ) {

        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,2);

        if (tok_fixStatus.p[0] == 'A')
        {
          Token  tok_time          = nmea_tokenizer_get(tzer,1);
          Token  tok_latitude      = nmea_tokenizer_get(tzer,3);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,4);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,5);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,6);
          Token  tok_speed         = nmea_tokenizer_get(tzer,7);
          Token  tok_bearing       = nmea_tokenizer_get(tzer,8);
          Token  tok_date          = nmea_tokenizer_get(tzer,9);

            nmea_reader_update_date( r, tok_date, tok_time );

            nmea_reader_update_latlong( r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0] );

            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);
        }

D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);

    }

    else if ( !memcmp(tok.p, "VTG", 3) ) {

        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,9);

        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != 'N')
        {
            Token  tok_bearing       = nmea_tokenizer_get(tzer,1);
            Token  tok_speed         = nmea_tokenizer_get(tzer,5);

            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);
        }
D(" [log hit][%s:%d] fix.flags=0x%x ", __FUNCTION__, __LINE__, r->fix.flags);
    }

    else if ( !memcmp(tok.p, "ZDA", 3) ) {

        Token  tok_time;
        Token  tok_year  = nmea_tokenizer_get(tzer,4);

        if (tok_year.p[0] != '\0') {

          Token  tok_day   = nmea_tokenizer_get(tzer,2);
          Token  tok_mon   = nmea_tokenizer_get(tzer,3);

          nmea_reader_update_cdate( r, tok_day, tok_mon, tok_year );

        }

        tok_time  = nmea_tokenizer_get(tzer,1);

        if (tok_time.p[0] != '\0') {

          nmea_reader_update_time(r, tok_time);

        }

    } else {
        tok.p -= 2;
        D_KEY("unknown sentence '%.*s", tok.end-tok.p, tok.p);
    }

#if GPS_DEBUG
    if (r->fix.flags != 0)
    {
        char   temp[256];
        char*  p   = temp;
        char*  end = p + sizeof(temp);
        struct tm   utc;

        p += snprintf( p, end-p, "sending fix" );
        if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG) {
            p += snprintf(p, end-p, " lat=%g lon=%g", r->fix.latitude, r->fix.longitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ALTITUDE) {
            p += snprintf(p, end-p, " altitude=%g", r->fix.altitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_SPEED) {
            p += snprintf(p, end-p, " speed=%g", r->fix.speed);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_BEARING) {
            p += snprintf(p, end-p, " bearing=%g", r->fix.bearing);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ACCURACY) {
            p += snprintf(p,end-p, " accuracy=%g", r->fix.accuracy);
        }
        gmtime_r( (time_t*) &r->fix.timestamp, &utc );
        p += snprintf(p, end-p, " time=%s", asctime( &utc ) );
        D_KEY(temp);
    }
#endif
}

#if 0 //Jerry: use memcpy instead of copy byte by byte to increase efficiency
static void
nmea_reader_addc( NmeaReader*  r, char  c )
{
    if (r->overflow) {
        r->overflow = (c != '\n');
        return;
    }

    if (r->pos >= (int) sizeof(r->in)-1 ) {
        r->overflow = 1;
        r->pos      = 0;
        return;
    }

    r->in[r->pos] = (char)c;
    r->pos       += 1;

    if (c == '\n') {
        GPS_STATE_LOCK_FIX(gps_state);
        nmea_reader_parse( r );
        GPS_STATE_UNLOCK_FIX(gps_state);
        r->pos = 0;
    }
}
#endif

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       C O N N E C T I O N   S T A T E                 *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

typedef struct {
    int                     init;
    GpsCallbacks            callbacks;  // callback function to report GPS data to upper layer
    AGpsCallbacks           agpsCallbacks;
    GpsNiCallbacks          gpsNiCallbacks;
    pthread_t               tmr_thread;
    int                     fix_freq;
    sem_t                   fix_sem;
    int                     first_fix;
    NmeaReader              reader;
} GpsState;

static GpsState  _gps_state[1];
static GpsState *gps_state = _gps_state;

#ifdef TIMER_THREAD
/* NMEA parser requires locks */
#define GPS_STATE_LOCK_FIX(_s)         \
{                                      \
  int ret;                             \
  do {                                 \
    ret = sem_wait(&(_s)->fix_sem);    \
  } while (ret < 0 && errno == EINTR);   \
}
#define GPS_STATE_UNLOCK_FIX(_s)       sem_post(&(_s)->fix_sem)

#else
#define GPS_STATE_LOCK_FIX(_s)
#define GPS_STATE_UNLOCK_FIX(_s)

#endif

enum {
  STATE_QUIT  = 0,
  STATE_INIT  = 1,
  STATE_START = 2,
  STATE_STOP  = 3
};

/* control function in sirf main.c */
typedef void (* sirf_nmea_callback)(char *msg, int msgLen);

/** Sirf module callback structure. */
typedef struct {
        sirf_nmea_callback nmea_cb;
} SirfCallbacks;

void gps_nmea_data_process(char *msg, int msgLen);

SirfCallbacks sCallbacks = {
    gps_nmea_data_process,
};
/* functions exported in Sirf module*/
#ifndef ANDROID_LOAD_SIRF_LIB
void *dlHandle = NULL;

int (*sirf_gps_init)(GpsCallbacks *cb);
void (*sirf_gps_cleanup)(void);
int (*sirf_gps_start)(void);
int (*sirf_gps_stop)(void);
int  (*sirf_gps_set_position_mode)(GpsPositionMode mode, int fix_frequency);
void (*sirf_agps_init)( AGpsCallbacks* callbacks);
int  (*sirf_agps_set_server)( AGpsType type, const char* hostname, int port);
void (*sirf_gps_ni_init)(GpsNiCallbacks *callbacks);
void (*sirf_gps_ni_respond)(int notif_id, GpsUserResponseType user_response);
void (*sirf_gps_delete_aiding_data)(GpsAidingData flags);
void (*sirf_gps_set_gprs_interface)(char *gprs_interface);
#else
extern int sirf_gps_init(GpsCallbacks *cb);
extern void sirf_gps_cleanup(void);
extern int sirf_gps_start(void);
extern int sirf_gps_stop(void);
extern int  sirf_gps_set_position_mode(GpsPositionMode mode, int fix_frequency);
extern void sirf_agps_init( AGpsCallbacks* callbacks);
extern int  sirf_agps_set_server( AGpsType type, const char* hostname, int port);
extern void sirf_gps_ni_init (GpsNiCallbacks *callbacks);
extern void sirf_gps_ni_respond (int notif_id, GpsUserResponseType user_response);
extern void sirf_gps_delete_aiding_data(GpsAidingData flags);
extern void sirf_gps_set_gprs_interface(char *gprs_interface);
#endif

#ifndef ANDROID_LOAD_SIRF_LIB

void clear_exported_func(void)
{
	if (dlHandle)
	{
		dlclose(dlHandle);
		dlHandle = NULL;
	}
	sirf_gps_init   = NULL;
	sirf_gps_cleanup = NULL;
	sirf_gps_start  = NULL;
	sirf_gps_stop   = NULL;
	sirf_gps_set_position_mode = NULL;
	sirf_agps_init       = NULL;
	sirf_agps_set_server = NULL;
	sirf_gps_ni_init     = NULL;
	sirf_gps_ni_respond  = NULL;
	sirf_gps_delete_aiding_data = NULL;
	sirf_gps_set_gprs_interface = NULL;
}

/* get_export_func

   return value: 0 : success; -1 fail
*/
int get_export_func(void)
{
	dlHandle = dlopen(SIRF_LIB_PATH, RTLD_NOW);
	if (NULL == dlHandle)
	{
		D("Can't load %s", SIRF_LIB_PATH);
		return -1;
	}

	sirf_gps_init = (int (*)(GpsCallbacks *))dlsym(dlHandle, "sirf_gps_init");
	sirf_gps_cleanup = (void (*)(void))dlsym(dlHandle, "sirf_gps_cleanup");
	sirf_gps_start = (int (*)(void))dlsym(dlHandle, "sirf_gps_start");
	sirf_gps_stop = (int (*)(void))dlsym(dlHandle, "sirf_gps_stop");
	sirf_gps_set_position_mode = (int (*)(GpsPositionMode, int))dlsym(dlHandle, "sirf_gps_set_position_mode");
	sirf_agps_init = (void (*)(AGpsCallbacks*))dlsym(dlHandle,"sirf_agps_init");
	sirf_agps_set_server = (int (*)( AGpsType, const char*, int))dlsym(dlHandle,"sirf_agps_set_server");
	sirf_gps_ni_init = (void (*)(GpsNiCallbacks *))dlsym(dlHandle,"sirf_gps_ni_init");
	sirf_gps_ni_respond = (void (*) (int, GpsUserResponseType))dlsym(dlHandle,"sirf_gps_ni_respond");
	sirf_gps_delete_aiding_data = (void (*)(GpsAidingData))dlsym(dlHandle, "sirf_gps_delete_aiding_data");
	sirf_gps_set_gprs_interface = (void (*)(char *))dlsym(dlHandle, "sirf_gps_set_gprs_interface");
	if (sirf_gps_init && sirf_gps_cleanup && sirf_gps_start &&
		sirf_gps_stop && sirf_gps_set_position_mode &&
		sirf_agps_init && sirf_agps_set_server &&
		sirf_gps_ni_init && sirf_gps_ni_respond &&
		sirf_gps_delete_aiding_data && sirf_gps_set_gprs_interface)
	{
		D("load %s successfully!", SIRF_LIB_PATH);
		return 0; // success
	}
	else
	{
		D("Can't load %s failed!", SIRF_LIB_PATH);
		clear_exported_func();
		return -1;
	}
}

#endif
static void
gps_report_data(GpsState *state)
{

    if (state->reader.fix.flags != 0) {

      D("gps fix cb: 0x%x", state->reader.fix.flags);

      if (state->callbacks.location_cb) {
          state->callbacks.location_cb( &state->reader.fix );
          state->reader.fix.flags = 0;
          state->first_fix = 1;
      }
      else {
        D("no callback, keeping location data until needed !");
      }

      if (state->fix_freq == 0) {
        state->fix_freq = -1;
      }
    }

    if (state->reader.sv_status_changed != 0) {

      D("gps sv status callback");

      if (state->callbacks.sv_status_cb) {
          state->callbacks.sv_status_cb( &state->reader.sv_status );
          state->reader.sv_status_changed = 0;
      }
      else {
        D("no callback, keeping status data until needed !");
      }

    }


}


#ifdef TIMER_THREAD
/* use seperate thread to report GPS data to implement fix frequency */
static void*
gps_timer_thread( void*  arg )
{

  GpsState *state = (GpsState *)arg;

  D_KEY("gps entered timer thread");

  do {

    D_KEY ("gps timer exp");

    GPS_STATE_LOCK_FIX(state);

    gps_report_data(state);

    GPS_STATE_UNLOCK_FIX(state);
    sleep(state->fix_freq);

  } while(state->init == STATE_START);

  D_KEY("gps timer thread destroyed");

  return NULL;

}
#endif

/* memory is allocated in sirf main.c */
void gps_nmea_data_process(char *msg, int msgLen)
{
    int i, flag;
    GpsState* state = _gps_state;
    NmeaReader  *r;

    D_LOW("enter %s: msgLen=%d", __FUNCTION__, msgLen);

    r = &state->reader;

    /* Copy to NMEA reader buf */
    if(msgLen > NMEA_MAX_SIZE)
    {
        D_KEY("%s: msg length(=%d) exceeds NMEA_MAX_SIZE!!", __FUNCTION__, msgLen);
        goto error;
    }

    r->pos = msgLen;
    memset(r->in, 0, NMEA_MAX_SIZE);
    memcpy(r->in, msg, msgLen);

    if(r->in[msgLen-1] != '\n')
    {
        D_KEY("%s: buf[msgLen-1] != '\n'. Buf=%s !!", __FUNCTION__, r->in);
    }

    /* Parse NMEA line. Use lock to avoid confliction with timer thread visit */
    GPS_STATE_LOCK_FIX(gps_state);

    nmea_reader_parse( r );

    if(r->fix.flags)
    {
/* If using seperate timer thread, report first fix data as soon as parsed */
#ifdef TIMER_THREAD
        if (!state->first_fix && state->init == STATE_INIT &&
        (state->reader.fix.flags & GPS_LOCATION_HAS_LAT_LONG))
        {
            gps_report_data(gps_state);
        }
#else
/* otherwise, report data as soon as parsed */
        gps_report_data(gps_state);
#endif
    }

    GPS_STATE_UNLOCK_FIX(gps_state);

    return;

error:
    D_KEY("Error! Leave %s directly!", __FUNCTION__);
    return;
}

static void
gps_state_done( GpsState*  state )
{
    state->init = STATE_QUIT;

    sirf_gps_cleanup();

    sem_destroy(&state->fix_sem);

    D_KEY("gps deinit complete");
}


static int
gps_state_start( GpsState*  state )
{
    int ret = 0;

    state->init = STATE_START;

    ret = sirf_gps_start();

    return ret;
}


static int
gps_state_stop( GpsState*  state )
{
    void*  dummy;
    int ret = 0;

    state->init = STATE_STOP;

#ifdef TIMER_THREAD
    pthread_join(state->tmr_thread, &dummy);
#endif

    ret = sirf_gps_stop();
    return ret;
}
#if 0
static int getInterfaceAddr(int cid, const char* ifname, char* ipaddress)
{
        int ret = -1;
        unsigned myaddr = 0;

        ifc_init();
        ifc_get_info(ifname, &myaddr, NULL, NULL);
        if ( myaddr )
        {
                ret = 0;
                sprintf(ipaddress, "%d.%d.%d.%d", myaddr & 0xFF, (myaddr >> 8) & 0xFF, (myaddr >> 16) & 0xFF, (myaddr >> 24) & 0xFF);
                LOGD("ppp0 IP address is: %s!\n", ipaddress);
        }
        ifc_close();
        return ret;
}
#endif
static void gps_set_gprs_interface(void)
{

    // check APN name here.
    static char gprs_ppp0[] = "ppp0";
    static char gprs_ccinet0[] = "ccinet0";
    static char *gprs_interface = NULL;
    char value[PROPERTY_VALUE_MAX];
    char ipaddress[64];
    int err = -1;
    int cid;

    cid = atoi("1");
    property_get("marvell.ril.ppp.enabled", value, "1");
    if (atoi(value))
    {
	    LOGD("PPP is used");
	    //Sometimes, gps_init is called before ppp is ready.

	    //err = getInterfaceAddr(cid, gprs_ppp0, ipaddress);
	    //if (err == 0)
	    {
		gprs_interface = gprs_ppp0;
	    }
    }
    else
    {
	    LOGD("Direct IP is used");
	    //err = getInterfaceAddr(cid, gprs_ccinet0, ipaddress);
	    //if (err == 0)
	    {
		gprs_interface = gprs_ccinet0;
	    }
    }
    LOGD("set gprs:%s to sirf module", gprs_interface);
    sirf_gps_set_gprs_interface(gprs_interface);
}
static int
gps_state_init( GpsState*  state )
{
    int ret = 0;
    NmeaReader  *reader;

    D("enter %s, line:%d", __FUNCTION__, __LINE__);

    state->init       = STATE_INIT;
    state->fix_freq   = -1;
    state->first_fix  = 0;

   if (sem_init(&state->fix_sem, 0, 1) != 0) {
      D_KEY("gps semaphore initialization failed! errno = %d", errno);
      ret = -1;
      goto END_INIT;
    }

    reader = &state->reader;
    nmea_reader_init( reader );


    D(" [log hit] %s:%d", __FUNCTION__, __LINE__);

    gps_set_gprs_interface();

#if SIRF_GSD3TW
    sirf_gps_init(&sCallbacks);
#else
    if (state)
    {
        ret = sirf_gps_init(&(state->callbacks));
    }
#endif
D(" [log hit] %s:%d", __FUNCTION__, __LINE__);

#ifdef TIMER_THREAD
    if ( pthread_create( &state->tmr_thread, NULL, gps_timer_thread, state ) != 0 )
    {
        LOGE("could not create gps timer thread: %s", strerror(errno));
    }
#endif

    D_LOW("leave %s: gps state initialized", __FUNCTION__);

END_INIT:
    return ret;

//Fail:
//    gps_state_done( state );
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       I N T E R F A C E                               *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/


static int
marvell_gps_init(GpsCallbacks* callbacks)
{
	int ret = 0;

    D_LOW("enter %s", __FUNCTION__);

#ifndef ANDROID_LOAD_SIRF_LIB
	ret = get_export_func();

	if (ret != 0)
		return ret;
#endif

    GpsState*  s = _gps_state;

	s->callbacks = *callbacks;

    if (!s->init)
        ret = gps_state_init(s);

   // s->callbacks = *callbacks;
    D_LOW("leave %s", __FUNCTION__);
    return ret;
}

static void
marvell_gps_cleanup(void)
{
    D_LOW("enter %s", __FUNCTION__);
    GpsState*  s = _gps_state;

    if (s->init)
        gps_state_done(s);

#ifndef ANDROID_LOAD_SIRF_LIB
	clear_exported_func();
#endif
    D_LOW("leave %s", __FUNCTION__);
}


static int
marvell_gps_start()
{
    int ret = 0;

    D_LOW("enter %s", __FUNCTION__);
    GpsState*  s = _gps_state;

    if (!s->init) {
        D_KEY("%s: called with uninitialized state !!", __FUNCTION__);
        return -1;
    }
    //gps_set_gprs_interface();
    ret = gps_state_start(s);

    D_LOW("leave %s", __FUNCTION__);
    return ret;
}


static int
marvell_gps_stop()
{
    int ret = 0;

    D_LOW("enter %s", __FUNCTION__);
    GpsState*  s = _gps_state;

    if (!s->init) {
        D_KEY("%s: called with uninitialized state !!", __FUNCTION__);
        return -1;
    }

    ret = gps_state_stop(s);

    D_LOW("leave %s", __FUNCTION__);
    return ret;
}

/** Injects the current time. */
static int
marvell_gps_inject_time(GpsUtcTime time, int64_t timeReference, int uncertainty)
{
    return 0;
}

/** Injects current location from another location provider
 *  (typically cell ID).
 *  latitude and longitude are measured in degrees
 *  expected accuracy is measured in meters
 */
static int
marvell_gps_inject_location(double latitude, double longitude, float accuracy)
{
    return 0;
}

/**
 * Specifies that the next call to start will not use the
 * information defined in the flags. GPS_DELETE_ALL is passed for
 * a cold start.
 */
static void
marvell_gps_delete_aiding_data(GpsAidingData flags)
{
	sirf_gps_delete_aiding_data(flags);
}

/**
 * fix_frequency represents the time between fixes in seconds.
 * Set fix_frequency to zero for a single-shot fix.
 */
static int marvell_gps_set_position_mode(GpsPositionMode mode, int fix_frequency)
{
    D_LOW("enter %s", __FUNCTION__);
    GpsState*  s = _gps_state;

    D("%s: GPS mode %d, fixfreq: %d", __FUNCTION__, mode, fix_frequency);
#if SIRF_GSD3TW
    // only standalone supported for now.
    if (mode != GPS_POSITION_MODE_STANDALONE)
    {
        D("%s: only standalone supported for now !!", __FUNCTION__);
        return -1;
    }

    if (!s->init || fix_frequency < 0)
    {
        D("%s: called with uninitialized state !!", __FUNCTION__);
        return -1;
    }

    s->fix_freq = fix_frequency;
#else
	sirf_gps_set_position_mode(mode, fix_frequency);
#endif
    D_LOW("gps fix frquency set to %d secs", fix_frequency);
    D_LOW("leave %s", __FUNCTION__);
    return 0;
}

/* Enter AGPS interface implement part
*/
static void  marvell_agps_init( AGpsCallbacks* callbacks )
{
    D("%s: called", __FUNCTION__);

    sirf_agps_init(callbacks);


}

static int  marvell_agps_data_conn_open( const char* apn )
{
    D("%s: called", __FUNCTION__);
    return 0;
}


static int  marvell_agps_data_conn_closed(void)
{
    D("%s: called", __FUNCTION__);
    return 0;
}


static int  marvell_agps_data_conn_failed(void)
{
    D("%s: called", __FUNCTION__);
    return 0;
}

static int  marvell_agps_set_server( AGpsType type, const char* hostname, int port )
{

    D("%s: called", __FUNCTION__);
	D("Type : %d, hostname = %s, port = %d", type, hostname, port);

	sirf_agps_set_server(type, hostname, port);
    return 0;

}
/* Out AGPS interface implement part
*/

static const AGpsInterface MarvellAGpsInterface =
{
	marvell_agps_init,
	marvell_agps_data_conn_open,
	marvell_agps_data_conn_closed,
	marvell_agps_data_conn_failed,
	marvell_agps_set_server
};

/* Enter AGPS interface implement part
*/
static void Marvell_gps_ni_init (GpsNiCallbacks *callbacks)
{

    D("%s: called", __FUNCTION__);

    sirf_gps_ni_init(callbacks);

    return;

}

static void Marvell_gps_ni_respond (int notif_id, GpsUserResponseType user_response)
{
    D("%s: called", __FUNCTION__);
    sirf_gps_ni_respond(notif_id, user_response);
    return;
}
/* Out AGPS interface implement part
*/

static const GpsNiInterface MarvellGpsNiInterface =
{
	Marvell_gps_ni_init,
	Marvell_gps_ni_respond
};


/** Get a pointer to extension information. Not used now */
static const void*
marvell_gps_get_extension(const char* name)
{
    D_LOW("enter %s", __FUNCTION__);
    D_LOW("leave %s", __FUNCTION__);

    D("%s: called", __FUNCTION__);

    if(0 == strcmp(name, AGPS_INTERFACE))
        return &MarvellAGpsInterface;

    if(0 == strcmp(name, GPS_NI_INTERFACE))
        return &MarvellGpsNiInterface;

    return NULL;
}

static const GpsInterface  marvellGpsInterface = {
    marvell_gps_init,
    marvell_gps_start,
    marvell_gps_stop,
    marvell_gps_cleanup,
    marvell_gps_inject_time,
    marvell_gps_inject_location,
    marvell_gps_delete_aiding_data,
    marvell_gps_set_position_mode,
    marvell_gps_get_extension,
};

const GpsInterface* gps_get_hardware_interface()
{
    D_LOW("enter %s", __FUNCTION__);
    D(" [log hit] %s:%d", __FUNCTION__, __LINE__);
    return &marvellGpsInterface;
}

