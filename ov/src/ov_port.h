#ifndef _H_os_port
#define _H_os_port

/* configuration */

#define OVHeap_TRACKING_OFF

/* headers */

#include<stdio.h>
#include<math.h>
#include<string.h>
#include<stdlib.h>
#include<ctype.h>

#ifndef OV_NULL
#define OV_NULL ((void*)0)
#endif

#ifndef OV_FALSE
#define OV_FALSE 0
#endif

#ifndef OV_TRUE
#define OV_TRUE 1
#endif

/* memory management */

#define ov_port_malloc malloc
#define ov_port_calloc calloc
#define ov_port_realloc realloc
#define ov_port_free free

/* termination */

#define ov_port_abort abort
#define ov_port_exit exit

/* header-dependent types */
#define ov_port_size_t size_t

#endif


