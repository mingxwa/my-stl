/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <memory>
#include <optional>
#include <thread>
#include <mutex>

#include "../../main/p0957/proxy.h"
#include "../../main/p0957/mock/proxy_progress_receiver_impl.h"

#include "../../main/experimental/thread_pool.h"

void MyLibrary(std::p0957::proxy<IProgressReceiver> p) {
  constexpr std::size_t kTotal = 500;
  try {
    std::this_thread::sleep_for(std::chrono::seconds(2));  // Mock initialization
    (*p).Initialize(kTotal);
    for (std::size_t i = 1u; i <= kTotal; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Mock job
      if ((*p).IsCanceled()) {
        throw std::runtime_error("Operation was canceled by peer");
      }
      (*p).UpdateProgress(i);
    }
  } catch (...) {
    (*p).OnException(std::current_exception());
  }
}

struct DemoContext {
 public:
  void Initialize(std::size_t total) {
    std::lock_guard<std::mutex> lk{mtx_};
    initialized_ = true;
    total_ = total;
  }

  void UpdateProgress(std::size_t progress) {
    std::lock_guard<std::mutex> lk{mtx_};
    progress_ = progress;
  }

  bool IsCanceled() noexcept {
    std::lock_guard<std::mutex> lk{mtx_};
    return canceled_;
  }

  void OnException(std::exception_ptr ex) noexcept {
    std::lock_guard<std::mutex> lk{mtx_};
    ex_ = std::move(ex);
  }

  void Cancel() noexcept {
    std::lock_guard<std::mutex> lk{mtx_};
    canceled_ = true;
  }

  void ReportOnConsole() {
    try {
      while (!IsInitialized()) {
        puts("Initializing...");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      puts("Initialized!");
      std::size_t total = GetTotal();
      std::size_t progress;
      do {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        progress = GetProgress();
        double percentage = 100. * progress / total;
        printf("Processed %zu out of %zu, %.1f%%\n", progress, total, percentage);
      } while (progress != total);
      puts("Done!");
    } catch (const std::exception& e) {
      printf("An exception was caught: %s\n", e.what());
    }
  }

 private:
  bool IsInitialized() const {
    std::lock_guard<std::mutex> lk{mtx_};
    if (ex_) {
      std::rethrow_exception(std::move(ex_));
    }
    return initialized_;
  }

  std::size_t GetTotal() const {
    std::lock_guard<std::mutex> lk{mtx_};
    return total_;
  }

  std::size_t GetProgress() const {
    std::lock_guard<std::mutex> lk{mtx_};
    if (ex_) {
      std::rethrow_exception(std::move(ex_));
    }
    return progress_;
  }

  mutable std::mutex mtx_;
  bool initialized_ = false;
  bool canceled_ = false;
  std::size_t total_ = 0u;
  std::size_t progress_ = 0u;
  std::exception_ptr ex_;
};

void DemoForThread() {
  DemoContext ctx;
  std::thread t{MyLibrary, &ctx};
  ctx.ReportOnConsole();
  t.join();
}

void DemoForThreadPool() {
  static std::experimental::static_thread_pool<> pool(2u);

  std::shared_ptr<DemoContext> ctx = std::make_shared<DemoContext>();
  pool.executor().execute([ctx]() mutable { MyLibrary(std::move(ctx)); });
  ctx->ReportOnConsole();
}

void DemoForThreadPoolWithCancellation() {
  static std::experimental::static_thread_pool<> pool(2u);

  std::shared_ptr<DemoContext> ctx = std::make_shared<DemoContext>();
  pool.executor().execute([ctx]() mutable { MyLibrary(std::move(ctx)); });
  pool.executor().execute([ctx]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    ctx->Cancel();
  });
  ctx->ReportOnConsole();
}

int main() {
  DemoForThread();
}
