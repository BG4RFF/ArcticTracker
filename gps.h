

#if !defined __DEF_NMEA_H__
#define __DEF_NMEA_H__


#include "defines.h"

/* Timestamp: Seconds since first day of month 00:00 */
typedef uint32_t timestamp_t; 

/* Position report */
typedef struct _PosData {    
    float latitude;
    float longitude;
    float speed, altitude;
    uint16_t course;
    timestamp_t timestamp;
} posdata_t;



/* Access to current position */

extern posdata_t current_pos;


/* API */
void     gps_init(SerialDriver*, Stream*);
uint32_t gps_distance(posdata_t*, posdata_t*);
uint16_t gps_bearing(posdata_t *from, posdata_t *to);
void     gps_mon_pos (void);
void     gps_mon_raw (void);
void     gps_mon_off (void);
bool     gps_is_fixed (void);
bool     gps_wait_fix (uint16_t);
char*    time2str (char*, timestamp_t);
void     gps_on(void);
void     gps_off(void);
bool     gps_hasWaiters(void);

#endif


