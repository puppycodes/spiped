#ifndef _PUSHBITS_H_
#define _PUSHBITS_H_

/**
 * pushbits(in, out):
 * Create a thread which copies data from ${in} to ${out}.  Return a cookie
 * which can be passed to pushbits_stop().
 */
void * pushbits(int, int);

/**
 * pushbits_free(push_cookie):
 * Free memory associated with the ${push_cookie}.
 */
void pushbits_free(void *);

/**
 * pushbits_cancel_free(push_cookie):
 * Cancel the thread in ${push_cookie} and free associated memory.
 */
void pushbits_cancel_free(void *);

#endif /* !_PUSHBITS_H_ */
