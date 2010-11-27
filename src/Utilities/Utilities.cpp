#include "Utilities.hpp"

#include <ctime>

const std::string GetISO8601Date(tm* date)
{
  char* buf = new char[80]; // magic number, 80 is enough for an ISO-8601 date
  strftime(buf, 80, "%Y-%m-%dT%H:%M:%S", date);
  std::string str(buf);
  delete [] buf;
  return str;
}