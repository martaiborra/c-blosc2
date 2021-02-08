/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: The Blosc Developers <blosc@blosc.org>
  Creation date: 2015-07-30

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "blosc2.h"
#include "blosc-private.h"
#include "context.h"
#include "frame.h"

#if defined(_WIN32) && !defined(__MINGW32__)
  #include <windows.h>
  #include <malloc.h>
  #include <direct.h>

  #define mkdir _mkdir

/* stdint.h only available in VS2010 (VC++ 16.0) and newer */
  #if defined(_MSC_VER) && _MSC_VER < 1600
    #include "win32/stdint-windows.h"
  #else
    #include <stdint.h>
  #endif

#endif  /* _WIN32 */

/* If C11 is supported, use it's built-in aligned allocation. */
#if __STDC_VERSION__ >= 201112L
  #include <stdalign.h>
#endif


/* Get the cparams associated with a super-chunk */
int blosc2_schunk_get_cparams(blosc2_schunk *schunk, blosc2_cparams **cparams) {
  *cparams = calloc(sizeof(blosc2_cparams), 1);
  (*cparams)->schunk = schunk;
  for (int i = 0; i < BLOSC2_MAX_FILTERS; i++) {
    (*cparams)->filters[i] = schunk->filters[i];
    (*cparams)->filters_meta[i] = schunk->filters_meta[i];
  }
  (*cparams)->compcode = schunk->compcode;
  (*cparams)->clevel = schunk->clevel;
  (*cparams)->typesize = schunk->typesize;
  (*cparams)->blocksize = schunk->blocksize;
  if (schunk->cctx == NULL) {
    (*cparams)->nthreads = BLOSC2_CPARAMS_DEFAULTS.nthreads;
  }
  else {
    (*cparams)->nthreads = (int16_t)schunk->cctx->nthreads;
  }
  return 0;
}


/* Get the dparams associated with a super-chunk */
int blosc2_schunk_get_dparams(blosc2_schunk *schunk, blosc2_dparams **dparams) {
  *dparams = calloc(sizeof(blosc2_dparams), 1);
  (*dparams)->schunk = schunk;
  if (schunk->dctx == NULL) {
    (*dparams)->nthreads = BLOSC2_DPARAMS_DEFAULTS.nthreads;
  }
  else {
    (*dparams)->nthreads = schunk->dctx->nthreads;
  }
  return 0;
}


void update_schunk_properties(struct blosc2_schunk* schunk) {
  blosc2_cparams* cparams = schunk->storage->cparams;
  blosc2_dparams* dparams = schunk->storage->dparams;

  for (int i = 0; i < BLOSC2_MAX_FILTERS; i++) {
    schunk->filters[i] = cparams->filters[i];
    schunk->filters_meta[i] = cparams->filters_meta[i];
  }
  schunk->compcode = cparams->compcode;
  schunk->clevel = cparams->clevel;
  schunk->typesize = cparams->typesize;
  schunk->blocksize = cparams->blocksize;
  schunk->chunksize = -1;

  /* The compression context */
  if (schunk->cctx != NULL) {
    blosc2_free_ctx(schunk->cctx);
  }
  cparams->schunk = schunk;
  schunk->cctx = blosc2_create_cctx(*cparams);

  /* The decompression context */
  if (schunk->dctx != NULL) {
    blosc2_free_ctx(schunk->dctx);
  }
  dparams->schunk = schunk;
  schunk->dctx = blosc2_create_dctx(*dparams);
}

/* Create a new super-chunk */
blosc2_schunk* blosc2_schunk_new(const blosc2_storage storage) {
  blosc2_schunk* schunk = calloc(1, sizeof(blosc2_schunk));
  schunk->version = 0;     /* pre-first version */

  // Get the storage with proper defaults
  schunk->storage = get_new_storage(&storage, &BLOSC2_CPARAMS_DEFAULTS, &BLOSC2_DPARAMS_DEFAULTS);
  // ...and update internal properties
  update_schunk_properties(schunk);

  if (!storage.contiguous && storage.urlpath != NULL){
    char* urlpath;
    char last_char = storage.urlpath[strlen(storage.urlpath) - 1];
    if (last_char == '\\' || last_char == '/') {
      urlpath = malloc(strlen(storage.urlpath));
      strncpy(urlpath, storage.urlpath, strlen(storage.urlpath) - 1);
      urlpath[strlen(storage.urlpath) - 1] = '\0';
    }
    else {
      urlpath = malloc(strlen(storage.urlpath) + 1);
      strcpy(urlpath, storage.urlpath);
    }
    // Create directory
    if (mkdir(urlpath, 0777) == -1) {
      BLOSC_TRACE_ERROR("Error during the creation of the directory, maybe it already exists.");
      return NULL;
    }
    // We want a sparse (directory) frame as storage
    blosc2_frame_s* frame = frame_new(urlpath);
    free(urlpath);
    frame->sframe = true;
    // Initialize frame (basically, encode the header)
    int64_t frame_len = frame_from_schunk(schunk, frame);
    if (frame_len < 0) {
      BLOSC_TRACE_ERROR("Error during the conversion of schunk to frame.");
      return NULL;
    }
    schunk->frame = (blosc2_frame*)frame;
  }
  if (storage.contiguous){
    // We want a contiguous frame as storage
    blosc2_frame_s* frame = frame_new(storage.urlpath);
    frame->sframe = false;
    // Initialize frame (basically, encode the header)
    int64_t frame_len = frame_from_schunk(schunk, frame);
    if (frame_len < 0) {
      BLOSC_TRACE_ERROR("Error during the conversion of schunk to frame.");
      return NULL;
    }
    schunk->frame = (blosc2_frame*)frame;
  }

  return schunk;
}


/* Create an empty super-chunk */
blosc2_schunk *blosc2_schunk_empty(int nchunks, const blosc2_storage storage) {
  blosc2_schunk* schunk = blosc2_schunk_new(storage);
  if (storage.contiguous) {
    BLOSC_TRACE_ERROR("Creating empty frames is not supported yet.");
    return NULL;
  }

  // Init offsets
  schunk->nchunks = nchunks;
  schunk->chunksize = -1;
  schunk->nbytes = 0;
  schunk->cbytes = 0;

  schunk->data_len += sizeof(void *) * nchunks;  // must be a multiple of sizeof(void*)
  schunk->data = calloc(nchunks, sizeof(void *));

  return schunk;
}


/* Create a copy of a super-chunk */
blosc2_schunk* blosc2_schunk_copy(blosc2_schunk *schunk, blosc2_storage storage) {
  if (schunk == NULL) {
    BLOSC_TRACE_ERROR("Can not copy a NULL `schunk`.");
    return NULL;
  }

  // Check if cparams are equals
  bool cparams_equal = true;
  blosc2_cparams cparams = {0};
  if (storage.cparams == NULL) {
    // When cparams are not specified, just use the same of schunk
    cparams.typesize = schunk->cctx->typesize;
    cparams.clevel = schunk->cctx->clevel;
    cparams.compcode = schunk->cctx->compcode;
    cparams.use_dict = schunk->cctx->use_dict;
    cparams.blocksize = schunk->cctx->blocksize;
    memcpy(cparams.filters, schunk->cctx->filters, BLOSC2_MAX_FILTERS);
    memcpy(cparams.filters_meta, schunk->cctx->filters_meta, BLOSC2_MAX_FILTERS);
    storage.cparams = &cparams;
  }
  else {
    cparams = *storage.cparams;
  }
  if (cparams.blocksize == 0) {
    // TODO: blocksize should be read from schunk->blocksize
    // For this, it should be updated during the first append
    // (or change API to make this a property during schunk creation).
    cparams.blocksize = schunk->cctx->blocksize;
  }

  if (cparams.typesize != schunk->cctx->typesize ||
      cparams.clevel != schunk->cctx->clevel ||
      cparams.compcode != schunk->cctx->compcode ||
      cparams.use_dict != schunk->cctx->use_dict ||
      cparams.blocksize != schunk->cctx->blocksize) {
    cparams_equal = false;
  }
  for (int i = 0; i < BLOSC2_MAX_FILTERS; ++i) {
    if (cparams.filters[i] != schunk->cctx->filters[i] ||
        cparams.filters_meta[i] != schunk->cctx->filters_meta[i]) {
      cparams_equal = false;
    }
  }

  // Create new schunk
  blosc2_schunk *new_schunk = blosc2_schunk_new(storage);
  if (new_schunk == NULL) {
    BLOSC_TRACE_ERROR("Can not create a new schunk");
    return NULL;
  }

  // Copy metalayers
  for (int nmeta = 0; nmeta < schunk->nmetalayers; ++nmeta) {
    blosc2_metalayer *meta = schunk->metalayers[nmeta];
    if (blosc2_add_metalayer(new_schunk, meta->name, meta->content, meta->content_len) < 0) {
      BLOSC_TRACE_ERROR("Con not add %s `metalayer`.", meta->name);
      return NULL;
    }
  }

  // Copy chunks
  if (cparams_equal) {
    for (int nchunk = 0; nchunk < schunk->nchunks; ++nchunk) {
      uint8_t *chunk;
      bool needs_free;
      if (blosc2_schunk_get_chunk(schunk, nchunk, &chunk, &needs_free) < 0) {
        BLOSC_TRACE_ERROR("Can not get the `chunk` %d.", nchunk);
        return NULL;
      }
      if (blosc2_schunk_append_chunk(new_schunk, chunk, !needs_free) < 0) {
        BLOSC_TRACE_ERROR("Can not append the `chunk` into super-chunk.");
        return NULL;
      }
    }
  } else {
    uint8_t *buffer = malloc(schunk->chunksize);
    for (int nchunk = 0; nchunk < schunk->nchunks; ++nchunk) {
      if (blosc2_schunk_decompress_chunk(schunk, nchunk, buffer, schunk->chunksize) < 0) {
        BLOSC_TRACE_ERROR("Can not decompress the `chunk` %d.", nchunk);
        return NULL;
      }
      if (blosc2_schunk_append_buffer(new_schunk, buffer, schunk->chunksize) < 0) {
        BLOSC_TRACE_ERROR("Can not append the `buffer` into super-chunk.");
        return NULL;
      }
    }
    free(buffer);
  }

  // Copy user meta
  if (schunk->usermeta != NULL) {
    uint8_t *usermeta;
    int32_t usermeta_len = blosc2_get_usermeta(schunk, &usermeta);
    if (usermeta_len < 0) {
      BLOSC_TRACE_ERROR("Can not get `usermeta` from schunk");
      return NULL;
    }
    if (blosc2_update_usermeta(new_schunk, usermeta, usermeta_len, cparams) < 0) {
      BLOSC_TRACE_ERROR("Can not update the `usermeta`.");
      return NULL;
    }
    free(usermeta);
  }
  return new_schunk;
}


/* Open an existing super-chunk that is on-disk (no copy is made). */
blosc2_schunk* blosc2_schunk_open(const char* urlpath) {
  if (urlpath == NULL) {
    BLOSC_TRACE_ERROR("You need to supply a urlpath.");
    return NULL;
  }

  blosc2_frame_s* frame = frame_from_file(urlpath);
  blosc2_schunk* schunk = frame_to_schunk(frame, false);

  // Set the storage with proper defaults
  size_t pathlen = strlen(urlpath);
  schunk->storage->urlpath = malloc(pathlen + 1);
  strcpy(schunk->storage->urlpath, urlpath);
  schunk->storage->contiguous = !frame->sframe;
  // Update the existing cparams/dparams with the new defaults
  update_schunk_properties(schunk);

  return schunk;
}


int64_t blosc2_schunk_to_buffer(blosc2_schunk* schunk, uint8_t** dest, bool* needs_free) {
  blosc2_frame_s* frame;
  int64_t cframe_len;
  if ((schunk->storage->contiguous == true) && (schunk->storage->urlpath == NULL)) {
    frame =  (blosc2_frame_s*)(schunk->frame);
    *dest = frame->cframe;
    cframe_len = frame->len;
    *needs_free = false;
  }
  else {
    // Copy to a contiguous storage
    blosc2_storage frame_storage = {.contiguous=true};
    blosc2_schunk* schunk_copy = blosc2_schunk_copy(schunk, frame_storage);
    if (schunk_copy == NULL) {
      BLOSC_TRACE_ERROR("Error during the conversion of schunk to buffer.");
      return BLOSC2_ERROR_SCHUNK_COPY;
    }
    frame = (blosc2_frame_s*)(schunk_copy->frame);
    *dest = frame->cframe;
    cframe_len = frame->len;
    *needs_free = true;
    frame->avoid_cframe_free = true;
    blosc2_schunk_free(schunk_copy);
  }

  return cframe_len;

}


/* Write an in-memory frame out to a file. */
int64_t frame_to_file(blosc2_frame_s* frame, const char* urlpath) {
  FILE* fp = fopen(urlpath, "wb");
  size_t nitems = fwrite(frame->cframe, (size_t)frame->len, 1, fp);
  fclose(fp);
  return nitems * (size_t)frame->len;
}


/* Write super-chunk out to a file. */
int64_t blosc2_schunk_to_file(blosc2_schunk* schunk, const char* urlpath) {
  if (urlpath == NULL) {
    BLOSC_TRACE_ERROR("urlpath cannot be NULL");
    return BLOSC2_ERROR_INVALID_PARAM;
  }

  // Accelerated path for in-memory frames
  if (schunk->storage->contiguous && schunk->storage->urlpath == NULL) {
    int64_t len = frame_to_file((blosc2_frame_s*)(schunk->frame), urlpath);
    if (len <= 0) {
      BLOSC_TRACE_ERROR("Error writing to file");
      return len;
    }
    return len;
  }

  // Copy to a contiguous file
  blosc2_storage frame_storage = {.contiguous=true, .urlpath=(char*)urlpath};
  blosc2_schunk* schunk_copy = blosc2_schunk_copy(schunk, frame_storage);
  if (schunk_copy == NULL) {
    BLOSC_TRACE_ERROR("Error during the conversion of schunk to buffer.");
    return BLOSC2_ERROR_SCHUNK_COPY;
  }
  blosc2_frame_s* frame = (blosc2_frame_s*)(schunk_copy->frame);
  int64_t frame_len = frame->len;
  blosc2_schunk_free(schunk_copy);
  return frame_len;
}


/* Free all memory from a super-chunk. */
int blosc2_schunk_free(blosc2_schunk *schunk) {
  if (schunk->data != NULL) {
    for (int i = 0; i < schunk->nchunks; i++) {
      free(schunk->data[i]);
    }
    free(schunk->data);
  }
  if (schunk->cctx != NULL)
    blosc2_free_ctx(schunk->cctx);
  if (schunk->dctx != NULL)
    blosc2_free_ctx(schunk->dctx);

  if (schunk->nmetalayers > 0) {
    for (int i = 0; i < schunk->nmetalayers; i++) {
      if (schunk->metalayers[i] != NULL) {
        if (schunk->metalayers[i]->name != NULL)
          free(schunk->metalayers[i]->name);
        if (schunk->metalayers[i]->content != NULL)
          free(schunk->metalayers[i]->content);
        free(schunk->metalayers[i]);
      }
    }
    schunk->nmetalayers = 0;
  }

  if (schunk->storage != NULL) {
    if (schunk->storage->urlpath != NULL) {
      free(schunk->storage->urlpath);
    }
    free(schunk->storage->cparams);
    free(schunk->storage->dparams);
    free(schunk->storage);
  }

  if (schunk->frame != NULL) {
    frame_free((blosc2_frame_s *) schunk->frame);
  }

  if (schunk->usermeta_len > 0) {
    free(schunk->usermeta);
  }

  free(schunk);

  return 0;
}


/* Create a super-chunk out of a contiguous frame buffer */
blosc2_schunk* blosc2_schunk_from_buffer(uint8_t *cframe, int64_t len, bool copy) {
  blosc2_frame_s* frame = frame_from_cframe(cframe, len, false);
  if (frame == NULL) {
    return NULL;
  }
  blosc2_schunk* schunk = frame_to_schunk(frame, copy);
  if (copy) {
    // We don't need the frame anymore
    frame_free(frame);
  }
  return schunk;
}


/* Append an existing chunk into a super-chunk. */
int blosc2_schunk_append_chunk(blosc2_schunk *schunk, uint8_t *chunk, bool copy) {
  int32_t nchunks = schunk->nchunks;
  int32_t nbytes = sw32_(chunk + BLOSC2_CHUNK_NBYTES);
  int32_t cbytes = sw32_(chunk + BLOSC2_CHUNK_CBYTES);

  if (schunk->chunksize == -1) {
    schunk->chunksize = nbytes;  // The super-chunk is initialized now
  }
  if (nbytes > schunk->chunksize) {
    BLOSC_TRACE_ERROR("Appending chunks that have different lengths in the same schunk "
                      "is not supported yet: %d > %d.", nbytes, schunk->chunksize);
    return BLOSC2_ERROR_CHUNK_APPEND;
  }

  /* Update counters */
  schunk->nchunks = nchunks + 1;
  schunk->nbytes += nbytes;
  if (schunk->frame == NULL) {
    schunk->cbytes += cbytes;
  } else {
    // A frame
    int special_value = (chunk[BLOSC2_CHUNK_BLOSC2_FLAGS] & 0x30) >> 4;
    switch (special_value) {
      case BLOSC2_ZERO_RUNLEN:
      case BLOSC2_NAN_RUNLEN:
        schunk->cbytes += 0;
        break;
      default:
        schunk->cbytes += cbytes;
    }
  }

  if (copy) {
    // Make a copy of the chunk
    uint8_t *chunk_copy = malloc(cbytes);
    memcpy(chunk_copy, chunk, cbytes);
    chunk = chunk_copy;
  }

  // Update super-chunk or frame
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame == NULL) {
    // Check that we are not appending a small chunk after another small chunk
    if ((schunk->nchunks > 0) && (nbytes < schunk->chunksize)) {
      uint8_t* last_chunk = schunk->data[nchunks - 1];
      int32_t last_nbytes = sw32_(last_chunk + BLOSC2_CHUNK_NBYTES);
      if ((last_nbytes < schunk->chunksize) && (nbytes < schunk->chunksize)) {
        BLOSC_TRACE_ERROR(
                "Appending two consecutive chunks with a chunksize smaller than the schunk chunksize "
                "is not allowed yet: %d != %d.", nbytes, schunk->chunksize);
        return BLOSC2_ERROR_CHUNK_APPEND;
      }
    }

    if (!copy && (cbytes < nbytes)) {
      // We still want to do a shrink of the chunk
      chunk = realloc(chunk, cbytes);
    }

    /* Make space for appending the copy of the chunk and do it */
    if ((nchunks + 1) * sizeof(void *) > schunk->data_len) {
      // Extend the data pointer by one memory page (4k)
      schunk->data_len += 4096;  // must be a multiple of sizeof(void*)
      schunk->data = realloc(schunk->data, schunk->data_len);
    }
    schunk->data[nchunks] = chunk;
  }
  else {
    if (frame_append_chunk(frame, chunk, schunk) == NULL) {
      BLOSC_TRACE_ERROR("Problems appending a chunk.");
      return BLOSC2_ERROR_CHUNK_APPEND;
    }
  }
  return schunk->nchunks;
}


/* Insert an existing @p chunk in a specified position on a super-chunk */
int blosc2_schunk_insert_chunk(blosc2_schunk *schunk, int nchunk, uint8_t *chunk, bool copy) {
  int32_t nchunks = schunk->nchunks;
  int32_t nbytes = sw32_(chunk + BLOSC2_CHUNK_NBYTES);
  int32_t cbytes = sw32_(chunk + BLOSC2_CHUNK_CBYTES);

  if (schunk->chunksize == -1) {
    schunk->chunksize = nbytes;  // The super-chunk is initialized now
  }

  if (nbytes > schunk->chunksize) {
    BLOSC_TRACE_ERROR("Inserting chunks that have different lengths in the same schunk "
                      "is not supported yet: %d > %d.", nbytes, schunk->chunksize);
    return BLOSC2_ERROR_CHUNK_INSERT;
  }

  /* Update counters */
  schunk->nchunks = nchunks + 1;
  schunk->nbytes += nbytes;
  if (schunk->frame == NULL) {
    schunk->cbytes += cbytes;
  } else {
    // A frame
    int special_value = (chunk[BLOSC2_CHUNK_BLOSC2_FLAGS] & 0x30) >> 4;
    switch (special_value) {
      case BLOSC2_ZERO_RUNLEN:
      case BLOSC2_NAN_RUNLEN:
        schunk->cbytes += 0;
        break;
      default:
        schunk->cbytes += cbytes;
    }
  }

  if (copy) {
    // Make a copy of the chunk
    uint8_t *chunk_copy = malloc(cbytes);
    memcpy(chunk_copy, chunk, cbytes);
    chunk = chunk_copy;
  }

  // Update super-chunk or frame
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame == NULL) {
    // Check that we are not appending a small chunk after another small chunk
    if ((schunk->nchunks > 0) && (nbytes < schunk->chunksize)) {
      uint8_t* last_chunk = schunk->data[nchunks - 1];
      int32_t last_nbytes = sw32_(last_chunk + BLOSC2_CHUNK_NBYTES);
      if ((last_nbytes < schunk->chunksize) && (nbytes < schunk->chunksize)) {
        BLOSC_TRACE_ERROR("Appending two consecutive chunks with a chunksize smaller "
                          "than the schunk chunksize is not allowed yet:  %d != %d",
                          nbytes, schunk->chunksize);
        return BLOSC2_ERROR_CHUNK_APPEND;
      }
    }

    if (!copy && (cbytes < nbytes)) {
      // We still want to do a shrink of the chunk
      chunk = realloc(chunk, cbytes);
    }

    // Make space for appending the copy of the chunk and do it
    if ((nchunks + 1) * sizeof(void *) > schunk->data_len) {
      // Extend the data pointer by one memory page (4k)
      schunk->data_len += 4096;  // must be a multiple of sizeof(void*)
      schunk->data = realloc(schunk->data, schunk->data_len);
    }

    // Reorder the offsets and insert the new chunk
    for (int i = nchunks; i > nchunk; --i) {
      schunk->data[i] = schunk->data[i-1];
    }
    schunk->data[nchunk] = chunk;
  }

  else {
    if (frame_insert_chunk(frame, nchunk, chunk, schunk) == NULL) {
      BLOSC_TRACE_ERROR("Problems inserting a chunk in a frame.");
      return BLOSC2_ERROR_CHUNK_INSERT;
    }
  }
  return schunk->nchunks;
}


int blosc2_schunk_update_chunk(blosc2_schunk *schunk, int nchunk, uint8_t *chunk, bool copy) {
  int32_t nbytes = sw32_(chunk + BLOSC2_CHUNK_NBYTES);
  int32_t cbytes = sw32_(chunk + BLOSC2_CHUNK_CBYTES);

  if (schunk->chunksize == -1) {
    schunk->chunksize = nbytes;  // The super-chunk is initialized now
  }

  if ((schunk->chunksize != 0) && (nbytes != schunk->chunksize)) {
    BLOSC_TRACE_ERROR("Inserting chunks that have different lengths in the same schunk "
                      "is not supported yet: %d > %d.", nbytes, schunk->chunksize);
    return BLOSC2_ERROR_CHUNK_INSERT;
  }

  bool needs_free;
  uint8_t *chunk_old;
  int err = blosc2_schunk_get_chunk(schunk, nchunk, &chunk_old, &needs_free);
  if (err < 0) {
    BLOSC_TRACE_ERROR("%d chunk con not obtenined from schunk.", nchunk);
  }
  int32_t cbytes_old;
  int32_t nbytes_old;

  if (chunk_old == 0) {
    nbytes_old = 0;
    cbytes_old = 0;
  } else {
    nbytes_old = sw32_(chunk_old + BLOSC2_CHUNK_NBYTES);
    cbytes_old = sw32_(chunk_old + BLOSC2_CHUNK_CBYTES);
    if (cbytes_old == BLOSC_MAX_OVERHEAD) {
        cbytes_old = 0;
    }
  }
  if (needs_free) {
    free(chunk_old);
  }

  if (copy) {
    // Make a copy of the chunk
    uint8_t *chunk_copy = malloc(cbytes);
    memcpy(chunk_copy, chunk, cbytes);
    chunk = chunk_copy;
  }

  blosc2_frame_s* frame = (blosc2_frame_s*)(schunk->frame);
  if (schunk->frame == NULL) {
    /* Update counters */
    schunk->nbytes += nbytes;
    schunk->nbytes -= nbytes_old;
    schunk->cbytes += cbytes;
    schunk->cbytes -= cbytes_old;
  } else {
    // A frame
    int special_value = (chunk[BLOSC2_CHUNK_BLOSC2_FLAGS] & 0x30) >> 4;
    switch (special_value) {
      case BLOSC2_ZERO_RUNLEN:
      case BLOSC2_NAN_RUNLEN:
        schunk->nbytes += nbytes;
        schunk->nbytes -= nbytes_old;
        if (frame->sframe) {
          schunk->cbytes -= cbytes_old;
        }
        break;
      default:
        /* Update counters */
        schunk->nbytes += nbytes;
        schunk->nbytes -= nbytes_old;
        schunk->cbytes += cbytes;
        schunk->cbytes -= cbytes_old;
    }
  }

  // Update super-chunk or frame
  if (schunk->frame == NULL) {
    if (!copy && (cbytes < nbytes)) {
      // We still want to do a shrink of the chunk
      chunk = realloc(chunk, cbytes);
    }

    // Free old chunk and add reference to new chunk
    if (schunk->data[nchunk] != 0) {
      free(schunk->data[nchunk]);
    }
    schunk->data[nchunk] = chunk;
  }
  else {
    if (frame_update_chunk(frame, nchunk, chunk, schunk) == NULL) {
        BLOSC_TRACE_ERROR("Problems updating a chunk in a frame.");
        return BLOSC2_ERROR_CHUNK_UPDATE;
    }
  }

  return schunk->nchunks;
}


/* Append a data buffer to a super-chunk. */
int blosc2_schunk_append_buffer(blosc2_schunk *schunk, void *src, int32_t nbytes) {
  uint8_t* chunk = malloc(nbytes + BLOSC_MAX_OVERHEAD);
  /* Compress the src buffer using super-chunk context */
  int cbytes = blosc2_compress_ctx(schunk->cctx, src, nbytes, chunk,
                                   nbytes + BLOSC_MAX_OVERHEAD);
  if (cbytes < 0) {
    free(chunk);
    return cbytes;
  }
  // We don't need a copy of the chunk, as it will be shrinked if necessary
  int nchunks = blosc2_schunk_append_chunk(schunk, chunk, false);
  if (nchunks < 0) {
    BLOSC_TRACE_ERROR("Error appending a buffer in super-chunk");
    return nchunks;
  }

  return nchunks;
}

/* Decompress and return a chunk that is part of a super-chunk. */
int blosc2_schunk_decompress_chunk(blosc2_schunk *schunk, int nchunk,
                                   void *dest, int32_t nbytes) {
  int chunksize;
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;

  if (frame == NULL) {
    if (nchunk >= schunk->nchunks) {
      BLOSC_TRACE_ERROR("nchunk ('%d') exceeds the number of chunks "
                        "('%d') in super-chunk.", nchunk, schunk->nchunks);
      return BLOSC2_ERROR_INVALID_PARAM;
    }
    uint8_t* src = schunk->data[nchunk];
    if (src == 0) {
      return 0;
    }

    int nbytes_ = sw32_(src + BLOSC2_CHUNK_NBYTES);
    if (nbytes < nbytes_) {
      BLOSC_TRACE_ERROR("Buffer size is too small for the decompressed buffer "
                        "('%d' bytes, but '%d' are needed).", nbytes, nbytes_);
      return BLOSC2_ERROR_INVALID_PARAM;
    }
    int cbytes = sw32_(src + BLOSC2_CHUNK_CBYTES);
    chunksize = blosc2_decompress_ctx(schunk->dctx, src, cbytes, dest, nbytes);
    if (chunksize < 0 || chunksize != nbytes_) {
      BLOSC_TRACE_ERROR("Error in decompressing chunk.");
      if (chunksize < 0)
        return chunksize;
      return BLOSC2_ERROR_FAILURE;
    }
  } else {
    chunksize = frame_decompress_chunk(schunk->dctx, frame, nchunk, dest, nbytes);
    if (chunksize < 0) {
      return chunksize;
    }
  }
  return chunksize;
}

/* Return a compressed chunk that is part of a super-chunk in the `chunk` parameter.
 * If the super-chunk is backed by a frame that is disk-based, a buffer is allocated for the
 * (compressed) chunk, and hence a free is needed.  You can check if the chunk requires a free
 * with the `needs_free` parameter.
 * If the chunk does not need a free, it means that a pointer to the location in the super-chunk
 * (or the backing in-memory frame) is returned in the `chunk` parameter.
 *
 * The size of the (compressed) chunk is returned.  If some problem is detected, a negative code
 * is returned instead.
*/
int blosc2_schunk_get_chunk(blosc2_schunk *schunk, int nchunk, uint8_t **chunk, bool *needs_free) {
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame != NULL) {
    return frame_get_chunk(frame, nchunk, chunk, needs_free);
  }

  if (nchunk >= schunk->nchunks) {
    BLOSC_TRACE_ERROR("nchunk ('%d') exceeds the number of chunks "
                      "('%d') in schunk.", nchunk, schunk->nchunks);
    return BLOSC2_ERROR_INVALID_PARAM;
  }

  *chunk = schunk->data[nchunk];
  if (*chunk == 0) {
    *needs_free = 0;
    return 0;
  }

  *needs_free = false;
  return sw32_(*chunk + BLOSC2_CHUNK_CBYTES);
}


/* Return a compressed chunk that is part of a super-chunk in the `chunk` parameter.
 * If the super-chunk is backed by a frame that is disk-based, a buffer is allocated for the
 * (compressed) chunk, and hence a free is needed.  You can check if the chunk requires a free
 * with the `needs_free` parameter.
 * If the chunk does not need a free, it means that a pointer to the location in the super-chunk
 * (or the backing in-memory frame) is returned in the `chunk` parameter.
 *
 * The size of the (compressed) chunk is returned.  If some problem is detected, a negative code
 * is returned instead.
*/
int blosc2_schunk_get_lazychunk(blosc2_schunk *schunk, int nchunk, uint8_t **chunk, bool *needs_free) {
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (schunk->frame != NULL) {
    return frame_get_lazychunk(frame, nchunk, chunk, needs_free);
  }

  if (nchunk >= schunk->nchunks) {
    BLOSC_TRACE_ERROR("nchunk ('%d') exceeds the number of chunks "
                      "('%d') in schunk.", nchunk, schunk->nchunks);
    return BLOSC2_ERROR_INVALID_PARAM;
  }

  *chunk = schunk->data[nchunk];
  if (*chunk == 0) {
    *needs_free = 0;
    return 0;
  }

  *needs_free = false;
  return sw32_(*chunk + BLOSC2_CHUNK_CBYTES);
}


/* Find whether the schunk has a metalayer or not.
 *
 * If successful, return the index of the metalayer.  Else, return a negative value.
 */
int blosc2_has_metalayer(blosc2_schunk *schunk, const char *name) {
  if (strlen(name) > BLOSC2_METALAYER_NAME_MAXLEN) {
    BLOSC_TRACE_ERROR("Metalayers cannot be larger than %d chars.", BLOSC2_METALAYER_NAME_MAXLEN);
    return BLOSC2_ERROR_INVALID_PARAM;
  }

  for (int nmetalayer = 0; nmetalayer < schunk->nmetalayers; nmetalayer++) {
    if (strcmp(name, schunk->metalayers[nmetalayer]->name) == 0) {
      return nmetalayer;
    }
  }
  return BLOSC2_ERROR_NOT_FOUND;
}

/* Reorder the chunk offsets of an existing super-chunk. */
int blosc2_schunk_reorder_offsets(blosc2_schunk *schunk, int *offsets_order) {
  // Check that the offsets order are correct
  bool *index_check = (bool *) calloc(schunk->nchunks, sizeof(bool));
  for (int i = 0; i < schunk->nchunks; ++i) {
    int index = offsets_order[i];
    if (index >= schunk->nchunks) {
      BLOSC_TRACE_ERROR("Index is bigger than the number of chunks.");
      free(index_check);
      return BLOSC2_ERROR_DATA;
    }
    if (index_check[index] == false) {
      index_check[index] = true;
    } else {
      BLOSC_TRACE_ERROR("Index is yet used.");
      free(index_check);
      return BLOSC2_ERROR_DATA;
    }
  }
  free(index_check);

  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame != NULL) {
    return frame_reorder_offsets(frame, offsets_order, schunk);
  }
  uint8_t **offsets = schunk->data;

  // Make a copy of the chunk offsets and reorder it
  uint8_t **offsets_copy = malloc(schunk->data_len);
  memcpy(offsets_copy, offsets, schunk->data_len);

  for (int i = 0; i < schunk->nchunks; ++i) {
    offsets[i] = offsets_copy[offsets_order[i]];
  }
  free(offsets_copy);

  return 0;
}


/**
 * @brief Flush metalayers content into a possible attached frame.
 *
 * @param schunk The super-chunk to which the flush should be applied.
 *
 * @return If successful, a 1 is returned. Else, return a negative value.
 */
// Initially, this was a public function, but as it is really meant to be used only
// in the schunk_add_metalayer(), I decided to convert it into private and call it
// implicitly instead of requiring the user to do so.  The only drawback is that
// each add operation requires a complete frame re-build, but as users should need
// very few metalayers, this overhead should be negligible in practice.
int metalayer_flush(blosc2_schunk* schunk) {
  int rc = 1;
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame == NULL) {
    return rc;
  }
  rc = frame_update_header(frame, schunk, true);
  if (rc < 0) {
    BLOSC_TRACE_ERROR("Unable to update metalayers into frame.");
    return rc;
  }
  rc = frame_update_trailer(frame, schunk);
  if (rc < 0) {
    BLOSC_TRACE_ERROR("Unable to update trailer into frame.");
    return rc;
  }
  return rc;
}


/* Add content into a new metalayer.
 *
 * If successful, return the index of the new metalayer.  Else, return a negative value.
 */
int blosc2_add_metalayer(blosc2_schunk *schunk, const char *name, uint8_t *content, uint32_t content_len) {
  int nmetalayer = blosc2_has_metalayer(schunk, name);
  if (nmetalayer >= 0) {
    BLOSC_TRACE_ERROR("Metalayer \"%s\" already exists.", name);
    return BLOSC2_ERROR_INVALID_PARAM;
  }

  // Add the metalayer
  blosc2_metalayer *metalayer = malloc(sizeof(blosc2_metalayer));
  char* name_ = malloc(strlen(name) + 1);
  strcpy(name_, name);
  metalayer->name = name_;
  uint8_t* content_buf = malloc((size_t)content_len);
  memcpy(content_buf, content, content_len);
  metalayer->content = content_buf;
  metalayer->content_len = content_len;
  schunk->metalayers[schunk->nmetalayers] = metalayer;
  schunk->nmetalayers += 1;

  int rc = metalayer_flush(schunk);
  if (rc < 0) {
    return rc;
  }

  return schunk->nmetalayers - 1;
}


/* Update the content of an existing metalayer.
 *
 * If successful, return the index of the new metalayer.  Else, return a negative value.
 */
int blosc2_update_metalayer(blosc2_schunk *schunk, const char *name, uint8_t *content, uint32_t content_len) {
  int nmetalayer = blosc2_has_metalayer(schunk, name);
  if (nmetalayer < 0) {
    BLOSC_TRACE_ERROR("Metalayer \"%s\" not found.", name);
    return nmetalayer;
  }

  blosc2_metalayer *metalayer = schunk->metalayers[nmetalayer];
  if (content_len > (uint32_t)metalayer->content_len) {
    BLOSC_TRACE_ERROR("`content_len` cannot exceed the existing size of %d bytes.", metalayer->content_len);
    return nmetalayer;
  }

  // Update the contents of the metalayer
  memcpy(metalayer->content, content, content_len);

  // Update the metalayers in frame (as size has not changed, we don't need to update the trailer)
  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame != NULL) {
    int rc = frame_update_header(frame, schunk, false);
    if (rc < 0) {
      BLOSC_TRACE_ERROR("Unable to update meta info from frame.");
      return rc;
    }
  }

  return nmetalayer;
}


/* Get the content out of a metalayer.
 *
 * The `**content` receives a malloc'ed copy of the content.  The user is responsible of freeing it.
 *
 * If successful, return the index of the new metalayer.  Else, return a negative value.
 */
int blosc2_get_metalayer(blosc2_schunk *schunk, const char *name, uint8_t **content,
                         uint32_t *content_len) {
  int nmetalayer = blosc2_has_metalayer(schunk, name);
  if (nmetalayer < 0) {
    BLOSC_TRACE_ERROR("Metalayer \"%s\" not found.", name);
    return nmetalayer;
  }
  *content_len = (uint32_t)schunk->metalayers[nmetalayer]->content_len;
  *content = malloc((size_t)*content_len);
  memcpy(*content, schunk->metalayers[nmetalayer]->content, (size_t)*content_len);
  return nmetalayer;
}


/* Update the content of the usermeta chunk. */
int blosc2_update_usermeta(blosc2_schunk *schunk, uint8_t *content, int32_t content_len,
                           blosc2_cparams cparams) {
  if ((uint32_t) content_len > (1u << 31u)) {
    BLOSC_TRACE_ERROR("content_len cannot exceed 2 GB.");
    return BLOSC2_ERROR_2GB_LIMIT;
  }

  // Compress the usermeta chunk
  void* usermeta_chunk = malloc(content_len + BLOSC_MAX_OVERHEAD);
  blosc2_context *cctx = blosc2_create_cctx(cparams);
  int usermeta_cbytes = blosc2_compress_ctx(cctx, content, content_len, usermeta_chunk,
                                            content_len + BLOSC_MAX_OVERHEAD);
  blosc2_free_ctx(cctx);
  if (usermeta_cbytes < 0) {
    free(usermeta_chunk);
    return usermeta_cbytes;
  }

  // Update the contents of the usermeta chunk
  if (schunk->usermeta_len > 0) {
    free(schunk->usermeta);
  }
  schunk->usermeta = malloc(usermeta_cbytes);
  memcpy(schunk->usermeta, usermeta_chunk, usermeta_cbytes);
  free(usermeta_chunk);
  schunk->usermeta_len = usermeta_cbytes;

  blosc2_frame_s* frame = (blosc2_frame_s*)schunk->frame;
  if (frame != NULL) {
    int rc = frame_update_trailer(frame, schunk);
    if (rc < 0) {
      return rc;
    }
  }

  return usermeta_cbytes;
}


/* Retrieve the usermeta chunk */
int32_t blosc2_get_usermeta(blosc2_schunk* schunk, uint8_t** content) {
  size_t nbytes, cbytes, blocksize;
  blosc_cbuffer_sizes(schunk->usermeta, &nbytes, &cbytes, &blocksize);
  *content = malloc(nbytes);
  blosc2_context *dctx = blosc2_create_dctx(BLOSC2_DPARAMS_DEFAULTS);
  int usermeta_nbytes = blosc2_decompress_ctx(dctx, schunk->usermeta, schunk->usermeta_len, *content, (int32_t)nbytes);
  blosc2_free_ctx(dctx);
  if (usermeta_nbytes < 0) {
    return usermeta_nbytes;
  }
  return (int32_t)nbytes;
}
