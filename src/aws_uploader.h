#pragma once

#include <Arduino.h>
#include <FS.h>

namespace AwsUploader {

// Sube un archivo de la SD a s3://AWS_S3_BUCKET/<key> con PUT + SigV4.
// `key` es la ruta dentro del bucket (sin slash inicial), por ejemplo:
//   "lilygo-a7670/session_1234567890.csv"
// `epochNow` debe ser el unix time UTC actual (para x-amz-date y skew).
// Devuelve true si HTTP status ∈ [200, 299].
bool putObjectFromSd(const char* sdPath, const char* key, uint32_t epochNow);

}
