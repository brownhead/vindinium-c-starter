#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

typedef enum {
  VINDINIUM_STATUS_OK = 0,
  VINDINIUM_STATUS_FAILURE,
  VINDINIUM_STATUS_CURL_FAILURE,
  VINDINIUM_STATUS_NULL_POINTER,
  VINDINIUM_STATUS_BAD_CONFIG,
  VINDINIUM_STATUS_BUFFER_TOO_SMALL,
} VindiniumStatus;

typedef struct {
  char const *endpoint;
  char const *key;
  unsigned turns;
  char const *map;
} VindiniumTrainingConfig;

char const *VINDINIUM_DEFAULT_TRAINING_ENDPOINT = "http://vindinium.org/api/training";

typedef struct {
  CURL *curl_handle;
  char const *endpoint;
  char const *key;
  unsigned current_turn;
  unsigned max_turns;
} VindiniumSession;

#define LOG printf

VindiniumStatus vindinium_cleanup_session(VindiniumSession *aSession) {
  if (aSession == NULL) {
    return VINDINIUM_STATUS_NULL_POINTER;
  }

  curl_easy_cleanup(aSession);
  return VINDINIUM_STATUS_OK;
}

VindiniumStatus vindinium_create_training_session(
    VindiniumSession **aSession, VindiniumTrainingConfig *aConfig) {
  if (aSession == NULL || aConfig == NULL) {
    return VINDINIUM_STATUS_NULL_POINTER;
  }

  if (aConfig->key == NULL || strlen(aConfig->key) == 0) {
    return VINDINIUM_STATUS_BAD_CONFIG;
  }

  // If an endpoint wasn't specified, use default
  char const *endpoint = NULL;
  if (aConfig->endpoint == NULL || strlen(aConfig->endpoint) == 0) {
    endpoint = VINDINIUM_DEFAULT_TRAINING_ENDPOINT;
  }

  // Convert the number of turns into a string
  unsigned const TURNS_STRING_MAX_SIZE = 16;
  char turns_string[TURNS_STRING_MAX_SIZE] = {0};
  if (aConfig->turns != 0) {
    int chars_written = snprintf(turns_string, TURNS_STRING_MAX_SIZE, "%d", aConfig->turns);
    if (chars_written >= TURNS_STRING_MAX_SIZE) {
      return VINDINIUM_STATUS_BUFFER_TOO_SMALL;
    }
  }

  CURL *curl_handle = curl_easy_init();
  if (curl_handle == NULL) {
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
    char const *field_name = raw_post_fields[i];
    char const *field_value = raw_post_fields[i + 1];
    if (field_value != NULL && strlen(field_value) > 0) {
      char *escaped_value = curl_easy_escape(curl_handle, field_value, 0);
      int chars_written = snprintf(post_payload + pos, POST_PAYLOAD_MAX_SIZE - pos, "%s=%s&",
                                   field_name, escaped_value);
      curl_free(escaped_value);

      pos += chars_written;
      if (pos >= POST_PAYLOAD_MAX_SIZE) {
        curl_easy_cleanup(curl_handle);
        return VINDINIUM_STATUS_BUFFER_TOO_SMALL;
      }
    }
  }

  {
    curl_easy_setopt(curl_handle, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_payload);
    CURLcode rc = curl_easy_perform(curl_handle);
    if (rc != CURLE_OK) {
      return VINDINIUM_STATUS_CURL_FAILURE;
    }
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
  config.key = "asdf";
  VindiniumStatus status = vindinium_create_training_session(&session, &config);
  printf("\nStatus=%d\n", status);

  return 0;
}
