#ifndef __wrapper_h__
#define __wrapper_h__

#if _MSC_VER < 1900
#define snprintf(dst,size,format,...) _snprintf(dst,size,format,##__VA_ARGS__)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
#endif //__wrapper_h__