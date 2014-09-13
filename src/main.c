#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "../json-parser/json.h"

typedef enum {
  VINDINIUM_STATUS_OK = 0,
  VINDINIUM_STATUS_FAILURE,
  VINDINIUM_STATUS_CURL_FAILURE,
  VINDINIUM_STATUS_NULL_POINTER,
  VINDINIUM_STATUS_BAD_CONFIG,
  VINDINIUM_STATUS_BUFFER_TOO_SMALL,
  VINDINIUM_STATUS_MALFORMED_REQUEST,
} VindiniumStatus;

typedef struct {
  char const *endpoint;
  char const *key;
  unsigned turns;
  char const *map;
} VindiniumTrainingConfig;

static unsigned const VINDINIUM_MAX_CONTENT_LENGTH = 65536;

char const VINDINIUM_DEFAULT_TRAINING_ENDPOINT[] = "http://vindinium.org/api/training";

typedef struct {
  CURL *curl_handle;
  char const *endpoint;
  char const *key;
  unsigned current_turn;
  unsigned max_turns;
} VindiniumSession;

typedef struct {
  void *data;
  unsigned size;
  unsigned capacity;
} VindiniumVector;

#define LOG printf

VindiniumStatus vindinium_cleanup_session(VindiniumSession *aSession) {
  if (aSession == NULL) {
    return VINDINIUM_STATUS_NULL_POINTER;
  }

  curl_easy_cleanup(aSession);
  return VINDINIUM_STATUS_OK;
}

static size_t _header_callback(char *buffer, size_t size, size_t nitems, void *user_data) {
  static char const header_name[] = "content-length:";
  static unsigned const HEADER_NAME_LENGTH = sizeof(header_name) - 1;

  unsigned const buffer_size = size * nitems;

  // Determine if this is the Content-Length header.
  if (buffer_size < HEADER_NAME_LENGTH) {
    return buffer_size;
  }
  for (unsigned i = 0; i < HEADER_NAME_LENGTH; ++i) {
    if (header_name[i] != tolower(buffer[i])) {
      return buffer_size;
    }
  }

  // Convert the length from a string to an integer
  static unsigned const VALUE_BUFFER_SIZE = 32;
  char value_buffer[VALUE_BUFFER_SIZE] = {0};
  unsigned const remaining = buffer_size - HEADER_NAME_LENGTH;
  if (remaining + 1 /* +1 for null-char */ > VALUE_BUFFER_SIZE) {
    LOG("Not enough space in value buffer.\n");
    return 0;
  }
  memcpy(value_buffer, buffer + HEADER_NAME_LENGTH, buffer_size - HEADER_NAME_LENGTH);
  unsigned long const content_length = strtoul(value_buffer, NULL, 10);
  if (content_length == 0) {
    LOG("Failed to convert string to number: %s.\n", value_buffer);
    return 0;
  } else if (content_length > VINDINIUM_MAX_CONTENT_LENGTH) {
    LOG("Content-Length has value (%lu) greater than max (%o).\n", content_length,
        VINDINIUM_MAX_CONTENT_LENGTH);
    return 0;
  }
  LOG("Got Content-Length header with value %lu.\n", content_length);

  // Resize the vector appropriately
  VindiniumVector *vector = (VindiniumVector *) user_data;
  unsigned new_capacity = content_length + 1;
  void *new_data = realloc(vector->data, new_capacity);
  if (new_data == NULL) {
    LOG("Call to realloc failed!\n");
    return 0;
  }
  vector->data = new_data;
  vector->capacity = new_capacity;

  return buffer_size;
}

// Called by libcurl when data comes in
static size_t _data_callback(char *buffer, size_t size, size_t nmemb, void *user_data) {
  unsigned const buffer_size = size * nmemb;

  // Ensure the vector is correctly sized
  VindiniumVector *vector = (VindiniumVector *) user_data;
  unsigned const needed_capacity = vector->size + buffer_size + 1;
  if (vector->capacity < needed_capacity) {
    LOG("Vector's capacity (%u) too small. Resizing to %u.\n", vector->capacity, needed_capacity);
    void *new_data = realloc(vector->data, needed_capacity);
    if (new_data == NULL) {
      LOG("Call to realloc failed!\n");
      return 0;
    }
    vector->data = new_data;
    vector->capacity = needed_capacity;
  }

  LOG("Writing %u bytes of data.\n", buffer_size);
  memcpy(vector->data + vector->size, buffer, buffer_size);
  ((char *) vector->data)[vector->size + buffer_size] = '\0';
  vector->size += buffer_size;

  return buffer_size;
}

VindiniumStatus vindinium_create_training_session(VindiniumSession **aSession,
                                                  VindiniumTrainingConfig *aConfig) {
  if (aSession == NULL || aConfig == NULL) {
    return VINDINIUM_STATUS_NULL_POINTER;
  }
  if (aConfig->key == NULL || strlen(aConfig->key) == 0) {
    return VINDINIUM_STATUS_BAD_CONFIG;
  }

  LOG("Creating new training session.\n");

  // If an endpoint wasn't specified, use default
  char const *endpoint = NULL;
  if (aConfig->endpoint == NULL || strlen(aConfig->endpoint) == 0) {
    LOG("Using default endpoint '%s'.\n", VINDINIUM_DEFAULT_TRAINING_ENDPOINT);
    endpoint = VINDINIUM_DEFAULT_TRAINING_ENDPOINT;
  }

  // Convert the number of turns into a string
  unsigned const TURNS_STRING_MAX_SIZE = 16;
  char turns_string[TURNS_STRING_MAX_SIZE] = {0};
  if (aConfig->turns != 0) {
    int chars_written = snprintf(turns_string, TURNS_STRING_MAX_SIZE, "%d", aConfig->turns);
    if (chars_written >= TURNS_STRING_MAX_SIZE) {
      LOG("Turns string buffer too small!\n");
      return VINDINIUM_STATUS_BUFFER_TOO_SMALL;
    }
  }

  // We need a curl session to escape the values below, so init one now
  CURL *curl_handle = curl_easy_init();
  if (curl_handle == NULL) {
    LOG("Could not create libcurl session.\n");
    return VINDINIUM_STATUS_CURL_FAILURE;
  }

  // The unescaped/raw fields we'll POST to the server
  unsigned const NUM_FIELDS = 3;
  char const *raw_post_fields[] = {
    "key", aConfig->key,
    "turns", turns_string,
    "map", aConfig->map,
  };

  // The actual data we'll send to the server
  unsigned const POST_PAYLOAD_MAX_SIZE = 512;
  char post_payload[POST_PAYLOAD_MAX_SIZE] = {0};

  // Build the payload by adding each of the post fields (being sure to URL escape each value)
  unsigned pos = 0;
  for (unsigned i = 0; i < NUM_FIELDS; ++i) {
    char const *field_name = raw_post_fields[i * 2];
    char const *field_value = raw_post_fields[i * 2 + 1];
    if (field_value != NULL && strlen(field_value) > 0) {
      char *escaped_value = curl_easy_escape(curl_handle, field_value, 0);
      LOG("Adding key-value pair to payload: ('%s', '%s').\n", field_name, escaped_value);
      int chars_written = snprintf(post_payload + pos, POST_PAYLOAD_MAX_SIZE - pos, "%s=%s&",
                                   field_name, escaped_value);
      curl_free(escaped_value);
      escaped_value = NULL;

      pos += chars_written;
      if (pos >= POST_PAYLOAD_MAX_SIZE) {
        LOG("Payload too large!\n");
        curl_easy_cleanup(curl_handle);
        return VINDINIUM_STATUS_BUFFER_TOO_SMALL;
      }
    }
  }

  curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_payload);
  curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint);

  // Prepare the callbacks so that we write all the received data into a vector
  VindiniumVector vector = {0};
  curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, _header_callback);
  curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, &vector);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _data_callback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &vector);

  // Actually perform the request
  {
    CURLcode rc = curl_easy_perform(curl_handle);
    if (rc != CURLE_OK) {
      LOG("Failed to make HTTP request.\n");
      free(vector.data);
      curl_easy_cleanup(curl_handle);
      return VINDINIUM_STATUS_CURL_FAILURE;
    }
  }

  long http_code = 0;
  {
    CURLcode rc = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
    if (rc != CURLE_OK || http_code == 0) {
      LOG("Failed to get HTTP response code.\n");
      free(vector.data);
      curl_easy_cleanup(curl_handle);
      return VINDINIUM_STATUS_CURL_FAILURE;
    }
  }

  if (http_code != 200) {
    LOG("Server returned status code %ld.\n", http_code);
    free(vector.data);
    curl_easy_cleanup(curl_handle);
    return VINDINIUM_STATUS_MALFORMED_REQUEST;
  }

  json_value *root = json_parse(vector.data, vector.size);
  if (root == NULL) {
    LOG("Could not parse server response as JSON.\n");
    free(vector.data);
    curl_easy_cleanup(curl_handle);
  }

  return VINDINIUM_STATUS_OK;
}

int main(void) {
  {
    CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
    if (rc != 0) {
      return 1;
    }
  }

  VindiniumSession *session = NULL;
  VindiniumTrainingConfig config = {0};
  config.key = "p2alvejh";
  VindiniumStatus status = vindinium_create_training_session(&session, &config);
  printf("\nStatus=%d\n", status);

  return 0;
}
