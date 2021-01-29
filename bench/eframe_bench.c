/*********************************************************************
  Benchmark for testing eframe vs frame.

  For usage instructions of this benchmark, please see:

    http://blosc.org/synthetic-benchmarks.html

  I'm collecting speeds for different machines, so the output of your
  benchmarks and your processor specifications are welcome!

  Author: The Blosc Developers <blosc@blosc.org>

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "blosc2.h"
#include <assert.h>

#define KB  1024
#define MB  (1024*KB)
#define GB  (1024*MB)

#define NCHUNKS 1000       /* number of chunks */
#define CHUNKSIZE (200 * 1000)

int nchunks = NCHUNKS;

#if defined(_WIN32)
#include <malloc.h>

#endif  /* defined(_WIN32) && !defined(__MINGW32__) */

void test_comparation() {
  blosc_timestamp_t last, current;
  double frame_append_time, eframe_append_time, frame_decompress_time, eframe_decompress_time;

  int64_t nbytes, cbytes;
  size_t isize = CHUNKSIZE * sizeof(int32_t);
  int dsize;
  float totalsize = (float)(isize * nchunks);
  int32_t* data = malloc(isize);
  int32_t* data_dest = malloc(isize);

  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;

  blosc2_schunk* schunk_eframe;
  blosc2_schunk* schunk_frame;


  /* Initialize the Blosc compressor */
  blosc_init();

  /* Create a frame container */
  cparams.typesize = sizeof(int32_t);

  cparams.nthreads = 1;
  dparams.nthreads = 1;

  blosc2_storage storage = {.sequential=false, .urlpath="dir.b2eframe", .cparams=&cparams, .dparams=&dparams};
  schunk_eframe = blosc2_schunk_new(storage);

  blosc2_storage storage2 = {.sequential=true, .urlpath="test_frame.b2frame", .cparams=&cparams, .dparams=&dparams};
  schunk_frame = blosc2_schunk_new(storage2);

  printf("Test comparation frame vs eframe with %d chunks.\n", nchunks);
  // Feed it with data
  eframe_append_time=0.0;
  frame_append_time=0.0;
  blosc_set_timestamp(&current);
  for (int nchunk = 0; nchunk < nchunks; nchunk++) {
    for (int i = 0; i < CHUNKSIZE; i++) {
      data[i] = i + nchunk * CHUNKSIZE;
    }
    blosc_set_timestamp(&current);
    blosc2_schunk_append_buffer(schunk_eframe, data, isize);
    blosc_set_timestamp(&last);
    eframe_append_time += blosc_elapsed_secs(current, last);

    blosc_set_timestamp(&current);
    blosc2_schunk_append_buffer(schunk_frame, data, isize);
    blosc_set_timestamp(&last);
    frame_append_time += blosc_elapsed_secs(current, last);
  }
  printf("[Eframe Compr] Elapsed time:\t %6.3f s.  Processed data: %.3f GB (%.3f GB/s)\n",
         eframe_append_time, totalsize / GB, totalsize / (GB * eframe_append_time));
  printf("[Frame Compr] Elapsed time:\t %6.3f s.  Processed data: %.3f GB (%.3f GB/s)\n",
         frame_append_time, totalsize / GB, totalsize / (GB * frame_append_time));

  /* Gather some info */
  nbytes = schunk_eframe->nbytes;
  cbytes = schunk_eframe->cbytes;
  printf("Compression super-chunk-eframe: %ld -> %ld (%.1fx)\n",
         (long)nbytes, (long)cbytes, (1. * nbytes) / cbytes);
  nbytes = schunk_frame->nbytes;
  cbytes = schunk_frame->cbytes;
  printf("Compression super-chunk-frame: %ld -> %ld (%.1fx)\n",
         (long)nbytes, (long)cbytes, (1. * nbytes) / cbytes);

  // Decompress the data
  // eframe
  blosc_set_timestamp(&current);
  for (int nchunk = 0; nchunk < nchunks; nchunk++) {
    dsize = blosc2_schunk_decompress_chunk(schunk_eframe, nchunk, (void *) data_dest, isize);
    if (dsize < 0) {
      printf("Decompression error eframe.  Error code: %d\n", dsize);
    }
    assert (dsize == (int)isize);
  }
  blosc_set_timestamp(&last);
  eframe_decompress_time = blosc_elapsed_secs(current, last);
  // frame
  blosc_set_timestamp(&current);
  for (int nchunk = 0; nchunk < nchunks; nchunk++) {
    dsize = blosc2_schunk_decompress_chunk(schunk_frame, nchunk, (void *) data_dest, isize);
    if (dsize < 0) {
      printf("Decompression error frame.  Error code: %d\n", dsize);
    }
    assert (dsize == (int)isize);
  }
  blosc_set_timestamp(&last);
  frame_decompress_time = blosc_elapsed_secs(current, last);

  printf("[Eframe Decompr] Elapsed time:\t %6.3f s.  Processed data: %.3f GB (%.3f GB/s)\n",
         eframe_decompress_time, totalsize / GB, totalsize / (GB * eframe_decompress_time));
  printf("[Frame Decompr] Elapsed time:\t %6.3f s.  Processed data: %.3f GB (%.3f GB/s)\n",
         frame_decompress_time, totalsize / GB, totalsize / (GB * frame_decompress_time));

  printf("Decompression successful!\n");

  printf("Successful roundtrip!\n");

  /* Remove directory */
  blosc2_remove_dir(storage.urlpath);
  /* Free blosc resources */
  free(data_dest);
  free(data);
  blosc2_schunk_free(schunk_eframe);
  blosc2_schunk_free(schunk_frame);
  /* Destroy the Blosc environment */
  blosc_destroy();

}

int main(int argc, char* argv[]) {
  int iter = 1;

  if (argc >= 4) {
    printf("Usage: ./eframe_bench [nchunks] [iterations]");
    exit(1);
  }
  else if (argc >= 2) {
    nchunks = (int)strtol(argv[1], NULL, 10);
  }
  if (argc == 3) {
    iter = (int)strtol(argv[2], NULL, 10);
  }

  // Run as much as iter tests
  for (int i = 0; i < iter; i++) {
    test_comparation();
  }

  return 0;
}
