#include <stdio.h>
#include <curl/curl.h>

typedef CURL VindiniumSession;

enum VindiniumStatus {
  VINDINIUM_STATUS_OK = 0,
  VINDINIUM_STATUS_FAILURE,
  VINDINIUM_STATUS_CURL_FAILURE,
  VINDINIUM_STATUS_NULL_POINTER,
};

typedef struct {
  char const *endpoint;
  char const *key;
  unsigned int turns;
  char const *map;
} VindiniumTrainingConfig;

char const *VINDINIUM_DEFAULT_TRAINING_ENDPOINT = "http://vindinium.org/api/training";

typedef struct {
  char const *endpoint;
  char const *key;
  unsigned int current_turn;
  unsigned int max_turns;
} VindiniumSession;


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

  CURL *curl_session = curl_easy_init();
  if (curl_session == NULL) {
    return VINDINIUM_STATUS_CURL_FAILURE;
  }

  char const *endpoint = NULL;
  if (aConfig->endpoint == NULL) {
    endpoint = VINDINIUM_DEFAULT_TRAINING_ENDPOINT;
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

  return 0;
}
