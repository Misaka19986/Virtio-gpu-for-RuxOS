#include "log.h"
#include "sys/queue.h"
#include "virtio.h"
#include "virtio_gpu.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

void *virtio_gpu_handler(void *dev) {
    VirtIODevice *vdev = (VirtIODevice *)dev;
    GPUDev *gdev = vdev->dev;
    GPUCommand *gcmd = NULL;

    uint32_t request_cnt = 0;

    /// time
    uint32_t time_measure_cnt = 0;
    struct timespec start;
    struct timespec end;
    double one_thousand_time = 0;

    pthread_mutex_lock(&gdev->queue_mutex);
    for (;;) {
        // Check if the device is closed
        if (gdev->close) {
            pthread_mutex_unlock(&gdev->queue_mutex);
            pthread_exit(NULL);
            return NULL;
        }

        // Try to get a command
        // Holding the lock at this point
        while (!TAILQ_EMPTY(&gdev->command_queue)) {
            gcmd = TAILQ_FIRST(&gdev->command_queue);
            TAILQ_REMOVE(&gdev->command_queue, gcmd, next);

            pthread_mutex_unlock(&gdev->queue_mutex);

            /// time
            clock_gettime(CLOCK_MONOTONIC, &start);

            // Release the lock and start processing
            virtio_gpu_simple_process_cmd(gcmd, vdev);
            // Notify the frontend and free memory after the command is
            // completed, iov is freed by virtio_gpu_simple_process_cmd

            /// time
            clock_gettime(CLOCK_MONOTONIC, &end);
            one_thousand_time += (end.tv_sec - start.tv_sec) * 1000 +
                                 (end.tv_nsec - start.tv_nsec) / 1e6;

            time_measure_cnt++;

            if (time_measure_cnt >= 1000) {
                double average_time = one_thousand_time / time_measure_cnt;
                log_info(
                    "%s: total time for 1000 requests: %f ms, average time: "
                    "%f ms",
                    __func__, one_thousand_time, average_time);

                time_measure_cnt = 0;
                one_thousand_time = 0;
            }

            request_cnt++;

            if (request_cnt >= VIRTIO_GPU_MAX_REQUEST_BEFORE_KICK) {
                // Processed a certain number of requests, kick the frontend
                virtio_inject_irq(&vdev->vqs[gcmd->from_queue]);
                request_cnt = 0;
                // log_info("%s: processed request >= 16, kick frontend",
                // __func__);
            }

            free(gcmd);

            // Since we are still in the processing loop, no need to worry about
            // losing awake
            // Re-acquire the lock on the next check
            pthread_mutex_lock(&gdev->queue_mutex);
        }

        if (request_cnt != 0) {
            // Processed requests but the task queue is empty, immediately kick
            // the frontend
            virtio_inject_irq(&vdev->vqs[gcmd->from_queue]);
            request_cnt = 0;
            // log_info("%s: request queue empty, kick frontend", __func__);
        }

        pthread_cond_wait(&gdev->gpu_cond, &gdev->queue_mutex);
    }
}