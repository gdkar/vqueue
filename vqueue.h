#ifndef __VQUEUE_H__
#define __VQUEUE_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct {
    size_t capacity;
    size_t big_mask;
    size_t small_mask;
    _Atomic(ptrdiff_t) rptr;
    _Atomic(ptrdiff_t) wptr;
    unsigned char * buffer;

    // "Private" members
    int _fd;
    void * _buffer_start;
    void * _buffer_middle;
} vq_t;

vq_t * vq_init(const char * name, size_t min_capacity);
void vq_destroy(vq_t * vq);

// --  "Zero-copy" write methods --
// vq_zcw_start: Get a buffer to write data into.
// - *vq: pointer to vqueue instance
// - **write_ptr: pointer to buffer pointer, populated with location to write data into
// return value: maximum amount that can be written into *write_ptr, 0 on error.
size_t vq_zcw_start(vq_t * vq, void ** write_ptr);

// vq_zcw_end: Call after writing to buffer from vq_zcw_start.
// - *vq: pointer to vqueue instance
// - length: amount of data written
// If length + vq->length > capacity, then data was silently overwritten
size_t vq_zcw_end(vq_t * vq, size_t length);

size_t vq_zcr_start(vq_t *vq, void const **read_ptr);
size_t vq_zcr_end(vq_t *vq, size_t length);
// --  "Generic" access methods --
// vq_read_fn: Function type passed to vq_generic_read
// - *opaque: user context
// - *data:   pointer to data the callback can use
// - length:  length of *data
// return value: amount of data to drop from the queue

typedef size_t (vq_read_fn) (void *opaque, const void *data, size_t length);
// - vq_write_fn: Function type passed to vq_generic_write
// - *opaque: user context
// - *data:   pointer to data the callback can write into
// - length:  length of *data
// return value: amount of data to commit to the queue
typedef size_t (vq_write_fn)(void *opaque, void *data, size_t length);

// vq_generic_read : generic interface read function
// - *vq: pointer to vqueue instance
// - *read_fn: pointer to callback
// - *opaque: pointer to context
// - length: requested read amount
// return value: amount read
size_t vq_generic_read(vq_t *vq, vq_read_fn *read_fn, void *opaque, size_t length);

// vq_generic_write : generic interface writefunction
// - *vq: pointer to vqueue instance
// - *write_fn: pointer to callback
// - *opaque: pointer to context
// - length: requested write amount
// return value: amount written
size_t vq_generic_write(vq_t *vq, vq_write_fn *read_fn, void *opaque, size_t length);

// vq_write: "Normal" write interface
size_t vq_write(vq_t * vq, const void * data, size_t length);

size_t vq_read(vq_t * vq, void * read_ptr, size_t length);


#endif
