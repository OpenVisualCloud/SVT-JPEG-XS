/*
* Copyright(c) 2019 Intel Corporation
* Copyright (c) 2019, Alliance for Open Media. All rights reserved
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#ifndef SystemResource_t_h
#define SystemResource_t_h
#include "SvtObject.h"
#include "SvtType.h"
#ifdef __cplusplus
extern "C" {
#endif
/*********************************
     * Defines
     *********************************/
#define ObjectWrapperReleasedValue ~0u

/*********************************************************************
      * Object Wrapper
      *   Provides state information for each type of object in the
      *   encoder system (i.e. SequenceControlSet, PictureBufferDesc,
      *   ProcessResults, and GracefulDegradation)
      *********************************************************************/
typedef struct ObjectWrapper {
    DctorCall dctor;

    DctorCall object_destroyer;
    // object_ptr - pointer to the object being managed.
    void *object_ptr;

    // live_count - a count of the number of pictures actively being
    //   encoded in the pipeline at any given time.  Modification
    //   of this value by any process must be protected by a mutex.
    uint32_t live_count;

    // release_enable - a flag that enables the release of
    //   ObjectWrapper_t for reuse in the encoding of subsequent
    //   pictures in the encoder pipeline.
    uint8_t release_enable;

    // system_resource_ptr - a pointer to the SystemResourceManager
    //   that the object belongs to.
    struct SystemResource *system_resource_ptr;

    // next_ptr - a pointer to a different ObjectWrapper_t.  Used
    //   only in the implementation of a single-linked Fifo.
    struct ObjectWrapper *next_ptr;
} ObjectWrapper_t;

/*********************************************************************
     * Fifo
     *   Defines a static (i.e. no dynamic memory allocation) single
     *   linked-list, constant time fifo implementation. The fifo uses
     *   the ObjectWrapper_t member next_ptr to create the linked-list.
     *   The Fifo also contains a counting_semaphore for OS thread-blocking
     *   and dynamic ObjectWrapper_t counting.
     *********************************************************************/
typedef struct Fifo {
    DctorCall dctor;
    // counting_semaphore - used for OS thread-blocking & dynamically
    //   counting the number of ObjectWrapper_ts currently in the
    //   Fifo_t.
    Handle_t counting_semaphore;

    // lockout_mutex - used to prevent more than one thread from
    //   modifying Fifo_t simultaneously.
    Handle_t lockout_mutex;

    // first_ptr - head pointer of the FIFO.
    ObjectWrapper_t *first_ptr;

    // last_ptr - pointer to the tail of the Fifo
    ObjectWrapper_t *last_ptr;

    // quit_signal - a flag that main thread sets to break out from kernels
    uint8_t quit_signal;

    // queue_ptr - pointer to MuxingQueue that the Fifo_t is
    //   associated with.
    struct MuxingQueue *queue_ptr;
} Fifo_t;

/*********************************************************************
     * CircularBuffer
     *********************************************************************/
typedef struct CircularBuffer {
    DctorCall dctor;
    void_ptr *array_ptr;
    uint32_t head_index;
    uint32_t tail_index;
    uint32_t buffer_total_count;
    uint32_t current_count;
} CircularBuffer_t;

/*********************************************************************
     * MuxingQueue
     *********************************************************************/
typedef struct MuxingQueue {
    DctorCall dctor;
    Handle_t lockout_mutex;
    CircularBuffer_t *object_queue;
    CircularBuffer_t *process_queue;
    uint32_t process_total_count;
    Fifo_t **process_fifo_ptr_array;
} MuxingQueue_t;

/*********************************************************************
     * SystemResource
     *   Defines a complete solution for managing objects in the encoder
     *   system (i.e. SequenceControlSet, PictureBufferDesc, ProcessResults, and
     *   GracefulDegradation).  The object_total_count and wrapper_ptr_pool are
     *   only used to construct and destruct the SystemResource.  The
     *   fullFifo provides downstream pipeline data flow control.  The
     *   emptyFifo provides upstream pipeline backpressure flow control.
     *********************************************************************/
typedef struct SystemResource {
    DctorCall dctor;
    // object_total_count - A count of the number of objects contained in the
    //   System Resource.
    uint32_t object_total_count;

    // wrapper_ptr_pool - An array of pointers to the ObjectWrapper_ts used
    //   to construct and destruct the SystemResource.
    ObjectWrapper_t **wrapper_ptr_pool;

    // The empty FIFO contains a queue of empty buffers
    MuxingQueue_t *empty_queue;

    // The full FIFO contains a queue of completed buffers
    MuxingQueue_t *full_queue;
} SystemResource_t;

/*********************************************************************
     * svt_jxs_system_resource_ctor
     *   Constructor for SystemResource_t.  Fully constructs all members
     *   of SystemResource_t including the object with the passed
     *   object_ctor function.
     *
     *   resource_ptr
     *     pointer that will contain the SystemResource to be constructed.
     *
     *   object_total_count
     *     Number of objects to be managed by the SystemResource.
     *
     *   object_ctor
     *     Function pointer to the constructor of the object managed by
     *     SystemResource referenced by resource_ptr. No object level
     *     construction is performed if object_ctor is NULL.
     *
     *   object_init_data_ptr
     *     pointer to data block to be used during the construction of
     *     the object. object_init_data_ptr is passed to object_ctor when
     *     object_ctor is called.
     *********************************************************************/
extern SvtJxsErrorType_t svt_jxs_system_resource_ctor(SystemResource_t *resource_ptr, uint32_t object_total_count,
                                                      uint32_t producer_process_total_count,
                                                      uint32_t consumer_process_total_count, Creator_t object_ctor,
                                                      void_ptr object_init_data_ptr, DctorCall object_destroyer);

/*********************************************************************
     * svt_jxs_system_resource_get_producer_fifo
     *   get producer fifo
     *
     *   resource_ptr
     *     pointer to SystemResource
     *
     *   index
     *     index to the producer fifo
     */
Fifo_t *svt_jxs_system_resource_get_producer_fifo(const SystemResource_t *resource_ptr, uint32_t index);

/*********************************************************************
     * svt_jxs_system_resource_get_consumer_fifo
     *   get producer fifo
     *
     *   resource_ptr
     *     pointer to SystemResource
     *
     *   index
     *     index to the consumer fifo
     */
Fifo_t *svt_jxs_system_resource_get_consumer_fifo(const SystemResource_t *resource_ptr, uint32_t index);

/*********************************************************************
     * SystemResource_tGetEmptyObject
     *   Dequeues an empty ObjectWrapper_t from the SystemResource.  The
     *   new ObjectWrapper_t will be populated with the contents of the
     *   wrapperCopyPtr if wrapperCopyPtr is not NULL. This function blocks
     *   on the SystemResource emptyFifo counting_semaphore. This function
     *   is write protected by the SystemResource emptyFifo lockout_mutex.
     *
     *   resource_ptr
     *      pointer to the SystemResource that provides the empty
     *      ObjectWrapper_t.
     *
     *   wrapper_dbl_ptr
     *      Double pointer used to pass the pointer to the empty
     *      ObjectWrapper_t pointer.
     *********************************************************************/
extern SvtJxsErrorType_t svt_jxs_get_empty_object(Fifo_t *empty_fifo_ptr, ObjectWrapper_t **wrapper_dbl_ptr);

extern SvtJxsErrorType_t svt_jxs_get_empty_object_non_blocking(Fifo_t *empty_fifo_ptr, ObjectWrapper_t **wrapper_dbl_ptr);

/*********************************************************************
     * SystemResource_tPostObject
     *   Queues a full ObjectWrapper_t to the SystemResource. This
     *   function posts the SystemResource fullFifo counting_semaphore.
     *   This function is write protected by the SystemResource fullFifo
     *   lockout_mutex.
     *
     *   resource_ptr
     *      pointer to the SystemResource that the ObjectWrapper_t is
     *      posted to.
     *
     *   wrapper_ptr
     *      pointer to ObjectWrapper_t to be posted.
     *********************************************************************/
extern SvtJxsErrorType_t svt_jxs_post_full_object(ObjectWrapper_t *object_ptr);

/*********************************************************************
     * SystemResource_tGetFullObject
     *   Dequeues an full ObjectWrapper_t from the SystemResource. This
     *   function blocks on the SystemResource fullFifo counting_semaphore.
     *   This function is write protected by the SystemResource fullFifo
     *   lockout_mutex.
     *
     *   resource_ptr
     *      pointer to the SystemResource that provides the full
     *      ObjectWrapper_t.
     *
     *   wrapper_dbl_ptr
     *      Double pointer used to pass the pointer to the full
     *      ObjectWrapper_t pointer.
     *********************************************************************/
extern SvtJxsErrorType_t svt_jxs_get_full_object(Fifo_t *full_fifo_ptr, ObjectWrapper_t **wrapper_dbl_ptr);

extern SvtJxsErrorType_t svt_jxs_get_full_object_non_blocking(Fifo_t *full_fifo_ptr, ObjectWrapper_t **wrapper_dbl_ptr);

/*********************************************************************
     * SystemResource_tReleaseObject
     *   Queues an empty ObjectWrapper_t to the SystemResource. This
     *   function posts the SystemResource emptyFifo counting_semaphore.
     *   This function is write protected by the SystemResource emptyFifo
     *   lockout_mutex.
     *
     *   object_ptr
     *      pointer to ObjectWrapper_t to be released.
     *********************************************************************/
extern SvtJxsErrorType_t svt_jxs_release_object(ObjectWrapper_t *object_ptr);

/*********************************************************************
     * svt_jxs_shutdown_process
     *   Notify shut down signal to consumer of SystemResource_t.
     *   So that the consumer process can break the loop and quit running.
     *
     *   resource_ptr
     *      pointer to the SystemResource.
     *********************************************************************/
extern SvtJxsErrorType_t svt_jxs_shutdown_process(const SystemResource_t *resource_ptr);

#define SVT_GET_FULL_OBJECT(full_fifo_ptr, wrapper_dbl_ptr)                          \
    do {                                                                             \
        SvtJxsErrorType_t err = svt_jxs_get_full_object(full_fifo_ptr, wrapper_dbl_ptr); \
        if (err == SvtJxsErrorNoErrorFifoShutdown)                                   \
            return NULL;                                                             \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif //SystemResource_t_h
