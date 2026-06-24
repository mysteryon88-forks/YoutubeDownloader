#pragma once

#include <chrono>
#include <stop_token>

bool WaitForDelay(std::stop_token stopToken, std::chrono::milliseconds delay);
