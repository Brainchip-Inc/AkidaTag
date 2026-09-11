#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>

namespace akida {

class PipelineState {
 public:
  // utility struct to store infos about dma jobs
  struct dma_job {
    uint32_t output_address;
    uint16_t id;
  };

  // utility struct to store infos about pipeline state
  struct slot {
    uint16_t job_id;
    size_t index;
  };

  PipelineState()
      : job_id_generated_(0),
        last_job_id_fetched_(0),
        current_index_(0),
        pipeline_size_(0) {}

  inline bool full() const { return current_jobs_.size() == pipeline_size_; }

  inline bool empty() const { return current_jobs_.empty(); }

  inline size_t max_size() const { return pipeline_size_; }

  void enqueue_job(uint16_t id, uint32_t output_address) {
    current_jobs_.push(dma_job{output_address, id});
  }

  dma_job pop_job() {
    auto job = current_jobs_.front();
    current_jobs_.pop();
    // update last_job_id_fetched
    last_job_id_fetched_ = job.id;
    return job;
  }

  slot reserve_job() {
    // increment job id first because it must start at 1 after reset to
    // differenciate it from last_job_id_fetched which start at 0
    ++job_id_generated_;
    ++current_index_;
    if (current_index_ >= pipeline_size_) {
      current_index_ = 0;
    }
    return slot{job_id_generated_, current_index_};
  }

  uint16_t last_job_fetched() const { return last_job_id_fetched_; }

  // reset should be called when dma is reset, because last_job_id_fetched may
  // not correspond to dma last job id processed
  // It also defines the max number of element in the pipeline
  void reset(uint16_t last_job_id, size_t pipeline_size) {
    last_job_id_fetched_ = last_job_id;
    job_id_generated_ = last_job_id;
    current_index_ = 0;
    pipeline_size_ = pipeline_size;
    // clear the queue
    current_jobs_ = std::queue<dma_job>();
  }

 protected:
  std::queue<dma_job> current_jobs_;

  uint16_t job_id_generated_;
  uint16_t last_job_id_fetched_;
  size_t current_index_;
  size_t pipeline_size_;
};

}  // namespace akida
