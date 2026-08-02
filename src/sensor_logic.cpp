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

  id = static_cast<int>(strtol(line, nullptr, 10));
  distance = static_cast<int>(strtol(comma + 1, nullptr, 10));
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
