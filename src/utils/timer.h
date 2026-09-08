#pragma once
#include <chrono>

namespace promatte {

class Stopwatch {
public:
	Stopwatch() : start_(clock::now()) {}
	void reset() { start_ = clock::now(); }
	double elapsedMs() const { return std::chrono::duration<double, std::milli>(clock::now() - start_).count(); }
	static double nowMs()
	{
		return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
	}

private:
	using clock = std::chrono::steady_clock;
	clock::time_point start_;
};

// Exponential moving average; cheap and allocation free.
class Ema {
public:
	explicit Ema(double alpha = 0.1) : alpha_(alpha) {}
	void add(double v)
	{
		if (!init_) {
			value_ = v;
			init_ = true;
		} else {
			value_ += alpha_ * (v - value_);
		}
		++count_;
	}
	double value() const { return value_; }
	bool initialized() const { return init_; }
	unsigned long long count() const { return count_; }
	void reset()
	{
		init_ = false;
		value_ = 0;
		count_ = 0;
	}

private:
	double alpha_;
	double value_ = 0;
	bool init_ = false;
	unsigned long long count_ = 0;
};

// Counts events per second using a sliding one-second window.
class RateCounter {
public:
	void tick(double nowMs)
	{
		++count_;
		if (windowStart_ < 0)
			windowStart_ = nowMs;
		double dt = nowMs - windowStart_;
		if (dt >= 1000.0) {
			rate_ = count_ * 1000.0 / dt;
			count_ = 0;
			windowStart_ = nowMs;
		}
	}
	// Call periodically so the rate decays to 0 when ticks stop.
	void idle(double nowMs)
	{
		if (windowStart_ >= 0 && nowMs - windowStart_ > 2000.0) {
			rate_ = count_ * 1000.0 / (nowMs - windowStart_);
			count_ = 0;
			windowStart_ = nowMs;
		}
	}
	double rate() const { return rate_; }
	void reset()
	{
		count_ = 0;
		windowStart_ = -1;
		rate_ = 0;
	}

private:
	unsigned count_ = 0;
	double windowStart_ = -1;
	double rate_ = 0;
};

} // namespace promatte
