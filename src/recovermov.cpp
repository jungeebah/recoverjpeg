/*
 * This file is part of the recoverjpeg program.
 *
 * Copyright (c) 2010-2016 Samuel Tardieu <sam@rfc1149.net>
 *                     and Jan Funke <jan.funke@inf.tu-dresden.de>.
 * http://www.rfc1149.net/devel/recoverjpeg
 *
 * recoverjpeg is released under the GNU General Public License
 * version 2 that you can find in the COPYING file bundled with the
 * distribution.
 */

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

size_t read_size(std::ifstream &infile) {

  size_t size = 0;
  unsigned char byte;

  byte = infile.get();
  size += byte << 24;
  byte = infile.get();
  size += byte << 16;
  byte = infile.get();
  size += byte << 8;
  byte = infile.get();
  size += byte;

  return size;
}

const std::string read_atom_type(std::ifstream &infile) {

  std::string type = "";
  char byte;

  for (int i = 0; i < 4; i++) {
    byte = infile.get();
    type += byte;
  }

  return type;
}

void copy_n(std::ifstream &infile, std::ofstream &outfile, size_t bytes) {

  while (bytes > 0) {

    /* naiv implementation */
    outfile.put(infile.get());

    bytes--;
  }
}

bool is_mov_file(std::ifstream &infile) {

  /* try to read the first atom type "ftyp" */
  infile.seekg(4, std::ios_base::cur);
  if (infile.fail() || infile.eof())
    return false;
  std::string atom_type = read_atom_type(infile);
  if (infile.fail() || infile.eof()) {
    infile.seekg(-4, std::ios_base::cur);
    return false;
  }

  /* reset read position */
  infile.seekg(-8, std::ios_base::cur);

  if (atom_type == "ftyp")
    return true;

  return false;
}

bool is_valid_atom_type(const std::string atom_type) {

  if (atom_type == "ftyp" || atom_type == "moov" || atom_type == "mdat" ||
      atom_type == "free" || atom_type == "skip" || atom_type == "wide" ||
      atom_type == "pnot")
    return true;

  return false;
}

void print_usage(int exitcode) {

  std::cerr << "Usage: recovermov [options] file|device\n";
  std::cerr << "Options:\n";
  std::cerr << "   -b blocksize   Block size in bytes\n";
  std::cerr << "                  (default: 512)\n";
  std::cerr << "   -n base_name   Basename of the mov files to create\n";
  std::cerr << "                  (default: \"video_\")\n";
  std::cerr << "   -h             This help message\n";
  std::cerr << "   -i index       Initial movie index\n";
  std::cerr << "   -o directory   Restore mov files into this directory\n";
  std::cerr << "   -q             Be quiet\n";
  std::cerr << "   -V             Display version and exit\n";
  exit(exitcode);
}

#ifdef LIBFUZZER_ENABLE
#define main fuzzer_main
#endif // LIBFUZZER_ENABLE

int main(int argc, const char *const argv[]) {

  size_t blocksize = 512;
  unsigned int mov_index = 0;
  std::string outfilebase = "video_";
  int quiet = 0;
  int dummy = 0;

#ifndef LIBFUZZER_ENABLE
  int c;
  while ((c = getopt(argc, argv, "b:hi:o:qV")) != -1) {
    switch (c) {
    case 'b':
      blocksize = atol_suffix(optarg);
      break;
    case 'n':
      outfilebase = optarg;
      break;
    case 'i':
      mov_index = atoi(optarg);
      break;
    case 'o':
      record_chdir(optarg);
      break;
    case 'q':
      quiet = 1;
      break;
    case 'V':
      display_version_and_exit("recovermov");
    default:
      print_usage((c == 'h' ? 0 : 1));
    }
  }

  argc -= optind;
  argv += optind;
#else  // !LIBFUZZER_ENABLE
  quiet = 1;
  dummy = 1;
#endif // !LIBFUZZER_ENABLE

  if (argc != 1) {
    print_usage(0);
  }

  const char *infilename = argv[0];

  std::ifstream infile(infilename, std::ios_base::in | std::ios_base::binary);

  perform_chdirs();

  size_t atom_size;
  std::string atom_type;

  while (!(infile.fail() || infile.eof())) {

    if (is_mov_file(infile)) {

      if (!quiet) {
        std::cout << "mov file detected\n";
      }
      mov_index++;

      std::ostringstream outfilename;
      outfilename << outfilebase << mov_index << ".mov";
      if (!quiet) {
        std::cout << "writing to " << outfilename.str() << "\n";
      }
      std::ofstream outfile(outfilename.str().c_str(),
                            std::ios_base::out | std::ios_base::binary);

      while (!infile.eof()) {

        atom_size = read_size(infile);
        atom_type = read_atom_type(infile);

        if (atom_size < 8) {
          if (!quiet) {
            std::cout << "encountered special atom (size=" << atom_size
                      << "), aborting\n";
          }
          break;
        }

        /* check whether we reached the end of the movie file */
        if (!is_valid_atom_type(atom_type))
          break;

        /* go back to beginning of atom */
        infile.seekg(-8, std::ios_base::cur);

        /* copy atom */
        copy_n(infile, outfile, atom_size);
      }

      if (!quiet) {
        std::cout << "recovery of " << outfilename.str() << " finished\n";
      }

      /* go back to last block start */
      size_t cur_pos = infile.tellg();
      size_t cur_block = cur_pos / blocksize;

      infile.seekg(cur_block * blocksize);
    }

    infile.seekg(blocksize, std::ios_base::cur);
  }

  return 0;
}

#ifdef LIBFUZZER_ENABLE

extern "C" {
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t length) {
#define TEMPLATE "/tmp/recovermov-fuzzXXXXXX"
  char filename[sizeof TEMPLATE] = TEMPLATE;
  mktemp(filename);
  int fd = open(filename, O_CREAT, 0644);
  write(fd, data, length);
  close(fd);
  const char *argv[] = {filename};
  main(1, argv);
  unlink(filename);
  return 0;
}
}

#endif // LIBFUZZER_ENABLE
