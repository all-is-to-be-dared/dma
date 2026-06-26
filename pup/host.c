#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <ctype.h>

#include "pup/host.h"
#include "pup/common.h"
#include "pup/config.h"




opts_t opts = {
  .self = "<UNINIT>",
  .inp_path = "<UNINIT>",
  .inp_fd = -1,
  .dev_path = "<UNINIT>",
  .dev_fd = -1,
  .load_addr = ~(uint64_t)0,
  .is_pty = false,
  .headless = false,
  ._print_usage = false,
};




void
parse_opts(int argc, char** argv)
{
  int r;

  opts.self = argv[0];
  enum
  {
    OPT_NONE,
    OPT_DEV,
    OPT_LOAD,
  } curr = OPT_NONE;
  if (argc == 1)
    opts._print_usage = true;
  for (int i = 1; i < argc; i++) {
    switch (curr) {
      case OPT_DEV:
        switch (find_serial_device(argv[i], &opts.dev_fd, BAUD_RATE)) {
          case FIND_OK:
            break;
          case FIND_NXDEV:
          case FIND_NOT_CHR:
          case FIND_OPEN_FAIL:
          default:
            fprintf(stderr, "TODO: IMPLEMENT ME: %s:%i\n", __FILE__, __LINE__);
            opts._print_usage = true;
            break;
        }
        curr = OPT_NONE;
        continue;
      case OPT_LOAD:
        errno = 0;
        char* end;
        opts.load_addr = strtoull(argv[i], &end, 0);
        if (*end) {
          fprintf(stderr,
                  "%s: couldn't parse %s value: stopped parsing at %c\n",
                  opts.self,
                  argv[i - 1],
                  *end);
          opts._print_usage = true;
        } else if (errno) {
          fprintf(
            stderr, "%s: couldn't parse %s value: %s\n", opts.self, argv[i - 1], strerror(errno));
          opts._print_usage = true;
        }
        curr = OPT_NONE;
        continue;
      case OPT_NONE:
        break;
    }
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
      opts._print_usage = true;
    else if(!strcmp(argv[i], "-H") || !strcmp(argv[i], "--headless"))
      opts.headless = true;
    else if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--pty"))
      opts.is_pty= true;
    else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--addr"))
      curr = OPT_LOAD;
    else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--dev"))
      curr = OPT_DEV;
    else {
      if (opts.inp_fd == -1) {
        opts.inp_path = argv[i];
        r = open(argv[i], O_RDONLY);
        if (r == -1) {
          fprintf(stderr,
                  "%s: couldn't open `%s`: %s (%d)\n",
                  opts.self,
                  argv[i],
                  strerror(errno),
                  errno);
          opts._print_usage = true;
        } else
          opts.inp_fd = r;
      } else {
        fprintf(stderr, "%s: unexpected argument `%s`\n", opts.self, argv[i]);
        opts._print_usage = true;
      }
    }
  }
  if (curr != OPT_NONE) {
    fprintf(stderr, "%s: expected argument after `%s`\n", opts.self, argv[argc - 1]);
    opts._print_usage = true;
  }
}




void
print_usage(void)
{
  printf("\n");
  printf("Usage: %s [-hHp] [-a <ADDR>] [-d <DEV>] <BINARY>\n", opts.self);
  printf("\n");
}




static void
hook_heartbeat(void)
{
  //
}

static void
hook_poll_acked(void)
{
  printf("\x1b[35mHOST\x1b[0m: POLL was acked\n");
}

static void
hook_sent_chunk(uint32_t no)
{
  //
}

static void
hook_passthru(uint8_t c)
{
  // printf("passthru: '%c' (<%d>)\n", c, c);
  if(isprint(c) || c == 0x1b || isspace(c) || c > 0x80)
    putchar(c);
  else
    printf("<%x>", c);
}




static uint8_t staging[0xffff];




static uint32_t
input_crc(void)
{
  ssize_t r;
  uint32_t crc;

  crc = CRC_INIT;
  while((r = read(opts.inp_fd, staging, sizeof staging)) != -1) {
    if(!r)
      break;
    crc = crc32(crc, staging, r);
  }

  if(r == -1) {
    fprintf(stderr,
            "%s: failed to read from %s: %s (%d)\n",
            opts.self,
            opts.inp_path,
            strerror(errno),
            errno);
    exit(1);
  }

  return crc;
}




bool
platform_marshal(size_t chunk_no, uint8_t** data, uint16_t* len)
{
  int r;

  r = pread(opts.inp_fd, staging, sizeof staging, chunk_no * sizeof staging);

  if (r == -1) {
    fprintf(stderr,
            "%s: failed to read from %s: %s (%d)\n",
            opts.self,
            opts.inp_path,
            strerror(errno),
            errno);
    return false;
  } else if (!r) {
    printf(HOST "ERROR: device requesting chunk %zu which is past the end of %s\n",
           chunk_no,
           opts.inp_path);
    return false;
  } else {
    assert(r > 0 && r < 0x1'0000);
    *len = r;
    *data = staging;
    return true;
  }
}




int
main(int argc, char** argv)
{
  size_t inp_size;
  uint32_t inp_crc;
  struct stat st;

  parse_opts(argc, argv);
  if (opts._print_usage) {
    print_usage();
    return EXIT_FAILURE;
  }

  if (opts.dev_fd == -1) {
    switch (find_serial_device(NULL, &opts.dev_fd, BAUD_RATE)) {
      case FIND_OK:
        break;
      case FIND_NXDEV:
      case FIND_NOT_CHR:
      case FIND_OPEN_FAIL:
      default:
        fprintf(stderr, "TODO: IMPLEMENT ME: %s:%i\n", __FILE__, __LINE__);
        print_usage();
        return EXIT_FAILURE;
    }
  }

  if (fstat(opts.inp_fd, &st) == -1) {
    fprintf(
      stderr, "%s: failed to stat %s: %s (%d)\n", opts.self, opts.inp_path, strerror(errno), errno);
    return EXIT_FAILURE;
  }
  inp_size = st.st_size;

  inp_crc = input_crc();

  meta_t meta = {
    .load_addr = opts.load_addr,
    .wire_size = inp_size,
    .mem_size = inp_size,
    .mem_crc32 = inp_crc,
  };
  struct uploader_hooks hooks = {
    .on_heartbeat = hook_heartbeat,
    .on_poll_acked = hook_poll_acked,
    .on_sent_chunk = hook_sent_chunk,
    .on_passthru = hook_passthru,
  };
  fsm_upload(&config, &meta, NULL, 4, hooks);

  if(!opts.headless) {
    close(opts.dev_fd);
    
    char baud_string[32];
    snprintf(baud_string, sizeof baud_string, "%"PRIi32, BAUD_RATE);
    execlp("picocom", "picocom", "--noreset", "-b", baud_string ,opts.dev_path, (char*)NULL);

    fprintf(stderr, "%s: failed to start picocom: %s (%d)\n", opts.self, strerror(errno), errno);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
