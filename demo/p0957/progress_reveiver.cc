/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <memory>
#include <thread>
#include <mutex>

#include "../../main/p0957/proxy.h"

struct Initialize : std::dispatch<
    void(std::size_t), [](auto& self, std::size_t total) { self.Initialize(total); }> {};
struct UpdateProgress : std::dispatch<
    void(std::size_t), [](auto& self, std::size_t progress) { self.UpdateProgress(progress); }> {};
struct IsCanceled : std::dispatch<
    bool(), [](auto& self) { return self.IsCanceled(); }> {};
struct OnException : std::dispatch<
    void(std::exception_ptr), [](auto& self, std::exception_ptr&& e) { self.OnException(std::move(e)); }> {};

struct FProgressReceiver : std::facade<
    Initialize, UpdateProgress, IsCanceled, OnException> {};

void MyLibrary(std::proxy<FProgressReceiver> p) {
  constexpr std::size_t kTotal = 500;
  try {
    std::this_thread::sleep_for(std::chrono::seconds(2));  // Mock init
    p.invoke<Initialize>(kTotal);
    for (std::size_t i = 1u; i <= kTotal; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Mock job
      if (p.invoke<IsCanceled>()) {
        throw std::runtime_error("Operation was canceled by peer");
      }
      p.invoke<UpdateProgress>(i);
    }
  } catch (...) {
    p.invoke<OnException>(std::current_exception());
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
        printf("Processed %zu out of %zu, %.1f%%\n",
            progress, total, percentage);
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

int main() {
  auto ctx = std::make_shared<DemoContext>();
  std::thread t1{MyLibrary, ctx};
  std::thread t2([ctx] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    puts("Canceling the work...");
    ctx->Cancel();
  });
  ctx->ReportOnConsole();
  t2.join();
  t1.join();
}
