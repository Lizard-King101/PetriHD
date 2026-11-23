#pragma once

#include <string>

namespace nursery {

// The progress page: one self-contained HTML file built around the run's
// data as JSON. While the run goes on, the page reloads itself.
std::string report_page(const std::string& json, bool running);

} // namespace nursery
