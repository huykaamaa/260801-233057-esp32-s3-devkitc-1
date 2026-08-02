#include "sensor_logic.h"

#include <cstdlib>
#include <cstring>

bool parseSensorLine(const char* line, int& id, int& distance)
{
  if (!line || line[0] == '\0') {
    return false;
  }

  size_t len = strlen(line);
  const char* comma = static_cast<const char*>(memchr(line, ',', len));
  if (!comma || comma == line) {
    return false;
  }

  // 4.5 fix: strtol() alone can't tell "0" from "no digits at all" (e.g. "ERR", "NaN",
  // or an empty field). Check endptr so a field with zero consumed digits is rejected
  // instead of silently becoming a fabricated 0 that then masquerades as a real reading.
  char* idEnd = nullptr;
  long idVal = strtol(line, &idEnd, 10);
  if (idEnd == line) {
    return false;
  }

  char* distEnd = nullptr;
  long distVal = strtol(comma + 1, &distEnd, 10);
  if (distEnd == comma + 1) {
    return false;
  }

  id = static_cast<int>(idVal);
  distance = static_cast<int>(distVal);
  return true;
}

bool isDistanceInRange(int distance, int minDistance, int maxDistance)
{
  return distance >= minDistance && distance <= maxDistance;
}

bool isValidDeviceId(int id, int deviceCount)
{
  return id >= 1 && id <= deviceCount;
}
