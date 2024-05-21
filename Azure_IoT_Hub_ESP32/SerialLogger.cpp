// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "SerialLogger.h"
#include <time.h>

#define UNIX_EPOCH_START_YEAR 1900

static String writeTime()
{
  struct tm* ptm;
  time_t now = time(NULL);

  ptm = gmtime(&now);

  String total = "";

  total += String(ptm->tm_year + UNIX_EPOCH_START_YEAR);
  total += String("-");
  total += String(ptm->tm_mon + 1);
  total += String("-");
  total += String(ptm->tm_mday);
  total += String(" ");

  if (ptm->tm_hour < 10)
  {
    total += String(0);
  }

  total += String(ptm->tm_hour);
  total += String(":");

  if (ptm->tm_min < 10)
  {
    total += String(0);
  }

  total += String(ptm->tm_min);
  total += String(":");

  if (ptm->tm_sec < 10)
  {
    total += String(0);
  }

  total += String(ptm->tm_sec);
  return total;
}

SerialLogger::SerialLogger() { Serial.begin(SERIAL_LOGGER_BAUD_RATE); }

void SerialLogger::Info(String message)
{
  Serial.print(writeTime() + " [INFO] ");
  Serial.println(message);
}

void SerialLogger::Error(String message)
{
  Serial.print(writeTime() + " [ERROR] ");
  Serial.println(message);
}

SerialLogger Logger;
