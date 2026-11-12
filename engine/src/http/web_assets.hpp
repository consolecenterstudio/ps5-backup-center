#pragma once
namespace pbc::web {
struct Asset { const char* path; const char* type; const unsigned char* data; unsigned long size; };
extern const Asset assets[];
extern const unsigned asset_count;
}  // namespace pbc::web
