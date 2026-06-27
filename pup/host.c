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
#include <lzma.h>

#include "pup/host.h"
#include "pup/common.h"
#include "pup/config.h"




opts_t opts = {
  .self = "<UNINIT>",
  .inp_path = "<UNINIT>",
  .inp_fd = -1,
  .dev_path = "<UNINIT>",
  .dev_fd = -1,
  .load_addr = 0x8000,
  .is_pty = false,
  .headless = false,
  .con_baud = 115200,
  .debug = DBG_MIN,
  .retries = 10,
  ._print_usage = false,
};




void
parse_opts(int argc, char** argv)
{
  int r;
  uint64_t nv;

  opts.self = argv[0];
  enum
  {
    OPT_NONE,
    OPT_DEV,
    OPT_LOAD,
    OPT_CON_BAUD,
    OPT_RETRIES,
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
      case OPT_RETRIES:
      case OPT_LOAD:
      case OPT_CON_BAUD:
        errno = 0;
        char* end;
        nv = strtoull(argv[i], &end, 0);
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
        } else {
          if (curr == OPT_LOAD)
            opts.load_addr = nv;
          else if (curr == OPT_CON_BAUD) {
            if (nv > UINT32_MAX) {
              fprintf(stderr, "%s: console baud rate too large (must be < 2^32)\n", opts.self);
              opts._print_usage = true;
            } else {
              opts.con_baud = nv;
            }
          }
          else if (curr == OPT_RETRIES) {
            if (nv > UINT8_MAX) {
              fprintf(stderr, "%s: console baud rate too large (must be < 256)\n", opts.self);
              opts._print_usage = true;
            } else {
              opts.retries = nv;
            }
          }
        }
        curr = OPT_NONE;
        continue;
      case OPT_NONE:
        break;
    }
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
      opts._print_usage = true;
    else if (!strcmp(argv[i], "-H") || !strcmp(argv[i], "--headless"))
      opts.headless = true;
    else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--pty"))
      opts.is_pty = true;
    else if (!strcmp(argv[i], "-B") || !strcmp(argv[i], "--console-baud"))
      curr = OPT_CON_BAUD;
    else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--addr"))
      curr = OPT_LOAD;
    else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--dev"))
      curr = OPT_DEV;
    else if (!strcmp(argv[i], "-D") || !strcmp(argv[i], "--debug"))
      opts.debug = DBG_FULL;
    else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
      opts.debug = DBG_NONE;
    else if (!strcmp(argv[i], "-R") || !strcmp(argv[i], "--retries"))
      opts.debug = OPT_RETRIES;
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




struct opt
{
  const char* short_;
  const char* long_;
  const char* help;
  const char* val;
  bool (*parse)(const char*);
};
static const struct opt NULL_OPT = { NULL, NULL, NULL, 0, NULL };




static const struct opt OPTS[] = {
  { "-h", "--help", "Show this help", NULL, nullptr },
  { "-d", "--dev", "Device to upload to", "<DEV>", nullptr },
  { "-p", "--pty", "Device is a PTY", NULL, nullptr },
  { "-H", "--headless", "Do not open post-upload console", NULL, nullptr },
  { "-B",
    "--console-baud",
    "Initial baud rate for post-upload console (115200)",
    "<BAUD>",
    nullptr },
  { "-a", "--addr", "Address at which to load binary (0x8000)", "<ADDR>", nullptr },
  { "-D", "--debug", "Enable debugging output", NULL, nullptr },
  { "-q", "--quiet", "Don't output anything", NULL, nullptr },
  { "-R", "--retries", "Don't output anything", NULL, nullptr },
  NULL_OPT,
};




void
print_usage(void)
{
  const struct opt* curr;

  printf("\x1b[1mP\x1b[0mrogram \x1b[1mUP\x1b[0mloader \"PUP\" v0.1.0\tMaximilien Cura\n");
  printf("\n");
  printf("Usage: pup [-hHp] [-B <CONBAUD>] [-a <ADDR>] [-d <DEV>] <BINARY>\n");
  printf("\n");
  printf("OPTIONS:\n");

  for (curr = OPTS; memcmp(curr, &NULL_OPT, sizeof *curr); curr++) {
    char buf[20];
    snprintf(buf, sizeof buf, "%s, %s", curr->short_, curr->long_);
    printf("\t%-20s\x1b[1m%-8s\x1b[0m%s\n", buf, curr->val ? curr->val : "", curr->help);
  }
  printf("\n");
  printf("NOTE: DEVICE SPECIFIERS\n");
  printf("\tDevices can be specified multiple ways:\n");
  printf("\t  - direct path to callout device\n");
  printf("\t  - TODO\n");
  printf("\n");
}




static int total_chunks, last_show;




static void
hook_heartbeat(void)
{
}

static void
hook_poll_acked(void)
{
  host_printf(DBG_MIN, "\x1b[35mHOST\x1b[0m: Device responded to poll\n");
}

static void
hook_sent_chunk(uint32_t no)
{
  uint32_t show, i;

  show = 80 * no / (total_chunks - 1);
  if (show != last_show && opts.debug == DBG_MIN) {
    // no / total_chunks * 80
    printf("\r[");
    for (i = 0; i < 80; i++) {
      if (i < show) {
        putchar('=');
      } else if (i == show) {
        putchar('>');
      } else {
        putchar(' ');
      }
    }
    printf("]");
    fflush(stdout);
  } else if (opts.debug == DBG_FULL) {
    printf("[");
    for (i = 0; i < 80; i++) {
      if (i < show) {
        putchar('=');
      } else if (i == show) {
        putchar('>');
      } else {
        putchar(' ');
      }
    }
    printf("]\n");
  }
  last_show = show;
}

static void
hook_all_chunks(void)
{
  if(opts.debug == DBG_MIN)
    printf("\n");
}

static void
hook_passthru(uint8_t c)
{
  if (opts.debug < DBG_FULL)
    return;

  if (isprint(c) || c == 0x1b || isspace(c) || c > 0x80)
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
  while ((r = read(opts.inp_fd, staging, sizeof staging)) != -1) {
    if (!r)
      break;
    crc = crc32(crc, staging, r);
  }

  if (r == -1) {
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
  ssize_t r;

  r = pread(opts.inp_fd, staging, sizeof staging, chunk_no * sizeof staging);

  if (r == -1) {
    fprintf(stderr,
            "%s: failed to read from %s: %s (%d)\n",
            opts.self,
            opts.inp_path,
            strerror(errno),
            errno);
    exit(1);
  } else if (!r) {
    fprintf(stderr,
            HOST "ERROR: device requesting chunk %zu which is past the end of %s\n",
            chunk_no,
            opts.inp_path);
    exit(1);
  } else {
    assert(r > 0 && r < 0x1'0000);
    *len = r;
    *data = staging;
    return true;
  }
}




static size_t
host_pread(int fd, void* buf, size_t len, off_t off)
{
  ssize_t r;

  r = pread(opts.inp_fd, buf, 6, 0);
  if (r == -1) {
    fprintf(stderr,
            "%s: failed to read from %s: %s (%d)\n",
            opts.self,
            opts.inp_path,
            strerror(errno),
            errno);
    exit(1);
  }

  return r;
}




static bool
is_xz_file(void)
{
  size_t r;
  char magic[6];

  const uint8_t XZ_SIG[6] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };

  r = host_pread(opts.inp_fd, magic, 6, 0);
  if (r < 6) {
    return false;
  } else {
    return !memcmp(XZ_SIG, magic, 6);
  }
}




static size_t
xz_uncompressed_size(int fd, size_t len)
{
  // Based on https://github.com/tukaani-project/xz/blob/master/doc/examples/11_file_info.c

  lzma_index* index;
  lzma_stream strm;
  lzma_ret r;
  ssize_t s;

  strm = (lzma_stream)LZMA_STREAM_INIT;
  r = lzma_file_info_decoder(&strm, &index, UINT64_MAX, len);
  switch (r) {
    case LZMA_OK:
      break;
    case LZMA_MEM_ERROR:
      fprintf(stderr,
              "%s: xz_uncompressed_size: LZMA_MEM_ERROR when initializing file info decoder\n",
              opts.self);
      exit(1);
    case LZMA_PROG_ERROR:
    default:
      fprintf(stderr,
              "%s: xz_uncompressed_size: lzma_file_info_decoder: unknown error (%d)\n",
              opts.self,
              r);
      exit(1);
  }

  strm.avail_in = 0;
  uint8_t inbuf[BUFSIZ];

  if (lseek(fd, 0, SEEK_SET) == -1) {
    fprintf(stderr,
            "%s: failed to rewind %s: %s (%d)\n",
            opts.self,
            opts.inp_path,
            strerror(errno),
            errno);
    exit(1);
  }

  while (1) {
    if (!strm.avail_in) {
      strm.next_in = inbuf;
      s = read(fd, inbuf, sizeof inbuf);
      if (s == -1) {
        fprintf(stderr,
                "%s: failed to read from %s: %s (%d)\n",
                opts.self,
                opts.inp_path,
                strerror(errno),
                errno);
        exit(1);
      }
      strm.avail_in = s;
    }

    r = lzma_code(&strm, LZMA_RUN);

    switch (r) {
      case LZMA_OK:
        break;
      case LZMA_SEEK_NEEDED:
        if (lseek(fd, strm.seek_pos, SEEK_SET) == -1) {
          fprintf(stderr,
                  "%s: failed to seek %s (%llu): %s (%d)\n",
                  opts.self,
                  opts.inp_path,
                  strm.seek_pos,
                  strerror(errno),
                  errno);
          exit(1);
        }
        strm.avail_in = 0;
        break;
      case LZMA_STREAM_END:
        return lzma_index_uncompressed_size(index);
      case LZMA_FORMAT_ERROR:
        fprintf(stderr, "%s: %s is not in the .xz format\n", opts.self, opts.inp_path);
        exit(1);
      case LZMA_OPTIONS_ERROR:
        fprintf(stderr,
                "%s: %s has .xz headers that are not supported by this liblzma version\n",
                opts.self,
                opts.inp_path);
        exit(1);
      case LZMA_DATA_ERROR:
        fprintf(stderr, "%s: %s is corrupt\n", opts.self, opts.inp_path);
        exit(1);
      case LZMA_MEM_ERROR:
        fprintf(stderr, "%s: xz_uncompressed_size: lzma_code: LZMA_MEM_ERROR\n", opts.self);
        exit(1);
      default:
        fprintf(
          stderr, "%s: xz_uncompressed_size: lzma_code: unexpected error (%d)\n", opts.self, r);
        exit(1);
    }
  }
}




int
main(int argc, char** argv)
{
  size_t inp_size, wire_size;
  uint32_t inp_crc;
  struct stat st;
  enum compress_type compress;
  enum fsm_upload_status sta;

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
  wire_size = st.st_size;
  total_chunks = (wire_size + 0xfffe) / 0xffff;

  inp_crc = input_crc();

  compress = COMPRESS_NONE;
  if (is_xz_file()) {
    host_printf(DBG_MIN, HOST "%s is XZ-compressed, switching compression modes\n", opts.inp_path);
    compress = COMPRESS_XZ;
    inp_size = xz_uncompressed_size(opts.inp_fd, wire_size);
    host_printf(DBG_MIN, HOST "%s inflates to %zuB\n", opts.inp_path, inp_size);
  } else {
    inp_size = wire_size;
  }

  meta_t meta = {
    .load_addr = opts.load_addr,
    .wire_size = wire_size,
    .mem_size = inp_size,
    .mem_crc32 = inp_crc,
    .compress = compress,
  };
  struct uploader_hooks hooks = {
    .on_heartbeat = hook_heartbeat,
    .on_poll_acked = hook_poll_acked,
    .on_sent_chunk = hook_sent_chunk,
    .on_passthru = hook_passthru,
    .on_all_chunks = hook_all_chunks,
  };
  sta = fsm_upload(&config, &meta, NULL, opts.retries, hooks);

  switch (sta) {
    case UPLOAD_OK:
      host_printf(DBG_MIN, HOST "Device booted succcessfully!\n");
      break;
    case UPLOAD_TIMEOUT:
      host_printf(DBG_MIN, HOST "Timed out trying to upload program!\n");
      return EXIT_FAILURE;
    case UPLOAD_FAILED:
      host_printf(DBG_MIN, HOST "Final pre-boot verification failed!\n");
      return EXIT_FAILURE;
    default:
      fprintf(stderr, "%s: unknown status from fsm_upload: %d\n", opts.self, sta);
      exit(1);
  }

  if (!opts.headless) {
    close(opts.dev_fd);

    char baud_string[32];
    snprintf(baud_string, sizeof baud_string, "%" PRIi32, opts.con_baud);
    execlp("picocom",
           "picocom",
           "--noreset",
           "--imap=lfcrlf",
           "-b",
           baud_string,
           opts.dev_path,
           (char*)NULL);

    fprintf(stderr, "%s: failed to start picocom: %s (%d)\n", opts.self, strerror(errno), errno);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
