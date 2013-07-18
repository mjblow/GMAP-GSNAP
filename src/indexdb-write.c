static char rcsid[] = "$Id: indexdb-write.c 101477 2013-07-15 15:33:07Z twu $";
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifndef HAVE_MEMCPY
# define memcpy(d,s,n) bcopy((s),(d),(n))
#endif
#ifndef HAVE_MEMMOVE
# define memmove(d,s,n) bcopy((s),(d),(n))
#endif

#include "indexdb-write.h"

#ifdef WORDS_BIGENDIAN
#include "bigendian.h"
#else
#include "littleendian.h"
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>		/* For memset */
#include <ctype.h>		/* For toupper */
#include <sys/mman.h>		/* For munmap */
#ifdef HAVE_UNISTD_H
#include <unistd.h>		/* For lseek and close */
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>		/* For off_t */
#endif
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include "mem.h"
#include "fopen.h"
#include "types.h"		/* For Oligospace_T */

#include "compress.h"
#include "complement.h"		/* For UPPERCASE_U2T and NO_UPPERCASE */
#include "genome_hr.h"		/* For read_gammas procedures */
#include "iit-read-univ.h"
#include "indexdb.h"



/* Another MONITOR_INTERVAL is in compress.c */
#define MONITOR_INTERVAL 100000000 /* 100 million nt */
#define OFFSETS_BUFFER_SIZE 1000000


/* Gammas */
#ifdef DEBUG
#define debug(x) x
#else
#define debug(x)
#endif

/* Writing of positions */
#ifdef DEBUG1
#define debug1(x) x
#else
#define debug1(x)
#endif



/************************************************************************
 *   Elias gamma representation
 ************************************************************************/

/* ctr is 32 at high bit and 1 at low bit */
static int
write_gamma (FILE *offsetscomp_fp, Offsetscomp_T *offsets_buffer, int offsets_buffer_size, int *offsets_buffer_i,
	     unsigned int *nwritten, Offsetscomp_T *buffer, int ctr, Offsetscomp_T gamma) {
  int length;
  Positionsptr_T nn;
  
  debug(printf("Entering write_gamma with gamma %u, ctr %d\n",gamma,ctr));

  gamma += 1;			/* To allow 0 to be represented */

  /* Compute length */
  length = 1;
  nn = 2;
  while (nn <= gamma) {
    length += 2;
    nn += nn;
  }
  debug(printf("gamma is %u (%08X), length is %u\n",gamma,gamma,length));


  /* Update buffer and write */
  while (length > ctr) {
    if (length - ctr < 32) {
      *buffer |= (gamma >> (length - ctr));
    }
    debug(printf("writing gamma %08X\n",*buffer));
    offsets_buffer[(*offsets_buffer_i)++] = *buffer;
    if (*offsets_buffer_i == offsets_buffer_size) {
      FWRITE_UINTS(offsets_buffer,offsets_buffer_size,offsetscomp_fp);
      *offsets_buffer_i = 0;
    }
    *nwritten += 1;
    length -= ctr;
    ctr = 32;
    *buffer = 0U;
  }
  
  debug(printf("  shifting gamma left by %d\n",ctr - length));
  *buffer |= (gamma << (ctr - length));
  debug(printf("  buffer is %08X\n",*buffer));
  ctr -= length;

  debug(printf("  returning ctr %d\n",ctr));
  return ctr;
}


void
Indexdb_write_gammaptrs (char *gammaptrsfile, char *offsetsfile, Positionsptr_T *offsets,
			 Oligospace_T oligospace, int blocksize) {
  FILE *offsets_fp;
  FILE *gammaptrs_fp, *offsetscomp_fp;
  Gammaptr_T *gammaptrs;
  int gammaptri;
  int j;
  Oligospace_T oligoi;

  Offsetscomp_T *offsets_buffer;
  int offsets_buffer_size = OFFSETS_BUFFER_SIZE;
  int offsets_buffer_i;

  Offsetscomp_T buffer;			/* gammabuffer */
  int ctr;
  Gammaptr_T nwritten;


  if (blocksize == 1) {
    /* Don't write gammaptrs.  Write offsetscomp in a single command. */
    if ((offsets_fp = FOPEN_WRITE_BINARY(offsetsfile)) == NULL) {
      fprintf(stderr,"Can't write to file %s\n",offsetsfile);
      exit(9);
    } else {
      FWRITE_UINTS(offsets,oligospace+1,offsets_fp);
    }

  } else {
    gammaptrs = (Gammaptr_T *) CALLOC(oligospace/blocksize+1,sizeof(Gammaptr_T));
    gammaptri = 0;

    if ((offsetscomp_fp = FOPEN_WRITE_BINARY(offsetsfile)) == NULL) {
      fprintf(stderr,"Can't write to file %s\n",offsetsfile);
      exit(9);
    }
    offsets_buffer = (Offsetscomp_T *) CALLOC(offsets_buffer_size,sizeof(Offsetscomp_T));
    offsets_buffer_i = 0;

    nwritten = 0U;
    for (oligoi = 0; oligoi < oligospace; oligoi += blocksize) {
      gammaptrs[gammaptri++] = nwritten;

      offsets_buffer[offsets_buffer_i++] = offsets[oligoi];
      if (offsets_buffer_i == offsets_buffer_size) {
	FWRITE_UINTS(offsets_buffer,offsets_buffer_size,offsetscomp_fp);
	offsets_buffer_i = 0;
      }
      nwritten += 1;

      if (blocksize > 1) {
	buffer = 0U;
	ctr = 32;
	for (j = 1; j < blocksize; j++) {
	  ctr = write_gamma(offsetscomp_fp,offsets_buffer,offsets_buffer_size,&offsets_buffer_i,
			    &nwritten,&buffer,ctr,offsets[oligoi+j]-offsets[oligoi+j-1]);
	}
	debug(printf("writing gamma %08X\n",buffer));
	offsets_buffer[offsets_buffer_i++] = buffer;
	if (offsets_buffer_i == offsets_buffer_size) {
	  FWRITE_UINTS(offsets_buffer,offsets_buffer_size,offsetscomp_fp);
	  offsets_buffer_i = 0;
	}
	nwritten += 1;
      }
    }


    /* Final entries for i == oligospace */
    gammaptrs[gammaptri++] = nwritten;
    if ((gammaptrs_fp = FOPEN_WRITE_BINARY(gammaptrsfile)) == NULL) {
      fprintf(stderr,"Can't write to file %s\n",gammaptrsfile);
      exit(9);
    } else {
      FWRITE_UINTS(gammaptrs,gammaptri,gammaptrs_fp);
      FREE(gammaptrs);
      fclose(gammaptrs_fp);
    }
    
    offsets_buffer[offsets_buffer_i++] = offsets[oligoi];
    if (offsets_buffer_i > 0) {
      FWRITE_UINTS(offsets_buffer,offsets_buffer_i,offsetscomp_fp);
      offsets_buffer_i = 0;
    }
    FREE(offsets_buffer);
    fclose(offsetscomp_fp);
    nwritten += 1;

  }

  return;
}


static void
check_offsets_from_gammas (char *gammaptrsfile, char *offsetscompfile, Positionsptr_T *offsets,
			   Oligospace_T oligospace, int blocksize) {
  Gammaptr_T *gammaptrs;
  Offsetscomp_T *offsetscomp;
  int gammaptrs_fd, offsetscomp_fd;
  size_t gammaptrs_len, offsetscomp_len;
  Oligospace_T oligoi, oligok;
  int j, p;
#ifndef HAVE_MMAP
  double seconds;
#endif

  Positionsptr_T *ptr, cum;
  int ctr;


#ifdef HAVE_MMAP
  gammaptrs = (Gammaptr_T *) Access_mmap(&gammaptrs_fd,&gammaptrs_len,gammaptrsfile,sizeof(Gammaptr_T),/*randomp*/false);
  offsetscomp = (Offsetscomp_T *) Access_mmap(&offsetscomp_fd,&offsetscomp_len,offsetscompfile,sizeof(Offsetscomp_T),/*randomp*/false);
#else
  gammaptrs = (Gammaptr_T *) Access_allocated(&gammaptrs_len,&seconds,gammaptrsfile,sizeof(Gammaptr_T));
  offsetscomp = (Offsetscomp_T *) Access_allocated(&offsetscomp_len,&seconds,offsetscompfile,sizeof(Offsetscomp_T));
#endif

  ptr = offsetscomp;
  oligok = 0UL;
  p = 0;

  for (oligoi = 0UL; oligoi < oligospace; oligoi += blocksize) {
#ifdef HAVE_MMAP
#ifdef WORDS_BIGENDIAN
    cum = Bigendian_convert_uint(*ptr++);
#else
    cum = *ptr++;
#endif
#else
    cum = *ptr++;
#endif

    if (offsetscomp[gammaptrs[p++]] != cum) {
#ifdef OLIGOSPACE_NOT_LONG
      fprintf(stderr,"Problem with gammaptrs at oligo %u: %u != %u.  Please inform twu@gene.com\n",
	      oligok,offsetscomp[gammaptrs[p-1]],cum);
#else
      fprintf(stderr,"Problem with gammaptrs at oligo %lu: %u != %u.  Please inform twu@gene.com\n",
	      oligok,offsetscomp[gammaptrs[p-1]],cum);
#endif
      exit(9);
    }

    if (offsets[oligok++] != cum) {
#ifdef OLIGOSPACE_NOT_LONG
      fprintf(stderr,"Problem with offsetscomp at oligo %u: %u != %u.  Please inform twu@gene.com\n",
	      oligok-1U,offsets[oligok-1],cum);
#else
      fprintf(stderr,"Problem with offsetscomp at oligo %lu: %u != %u.  Please inform twu@gene.com\n",
	      oligok-1UL,offsets[oligok-1],cum);
#endif
      exit(9);
    }

    ctr = 0;
    for (j = 1; j < blocksize; j++) {
#ifdef HAVE_MMAP
#ifdef WORDS_BIGENDIAN
      ctr = Genome_read_gamma_bigendian(&ptr,ctr,&cum);
#else
      ctr = Genome_read_gamma(&ptr,ctr,&cum);
#endif
#else
      ctr = Genome_read_gamma(&ptr,ctr,&cum);
#endif

      if (offsets[oligok++] != cum) {
#ifdef OLIGOSPACE_NOT_LONG
	fprintf(stderr,"Problem with offsetscomp at oligo %u: %u != %u.  Please inform twu@gene.com\n",
		oligok-1U,offsets[oligok-1],cum);
#else
	fprintf(stderr,"Problem with offsetscomp at oligo %lu: %u != %u.  Please inform twu@gene.com\n",
		oligok-1UL,offsets[oligok-1],cum);
#endif
	exit(9);
      }
    }
    if (ctr > 0) {
      ptr++;			/* Done with last gamma byte */
    }
  }


#ifdef HAVE_MMAP
#ifdef WORDS_BIGENDIAN
  cum = Bigendian_convert_uint(*ptr++);
#else
  cum = *ptr++;
#endif
#else
  cum = *ptr++;
#endif

  if (offsetscomp[gammaptrs[p]] != cum) {
#ifdef OLIGOSPACE_NOT_LONG
    fprintf(stderr,"Problem with gammaptrs at oligo %u: %u != %u.  Please inform twu@gene.com\n",
	    oligok,offsetscomp[gammaptrs[p-1]],cum);
#else
    fprintf(stderr,"Problem with gammaptrs at oligo %lu: %u != %u.  Please inform twu@gene.com\n",
	    oligok,offsetscomp[gammaptrs[p-1]],cum);
#endif
    exit(9);
  }
    
  if (offsets[oligok] != cum) {
#ifdef OLIGOSPACE_NOT_LONG
    fprintf(stderr,"Problem with offsetscomp at oligo %u: %u != %u.  Please inform twu@gene.com\n",
	    oligok,offsets[oligok],*ptr);
#else
    fprintf(stderr,"Problem with offsetscomp at oligo %lu: %u != %u.  Please inform twu@gene.com\n",
	    oligok,offsets[oligok],*ptr);
#endif
    exit(9);
  }

#ifdef HAVE_MMAP
  munmap((void *) offsetscomp,offsetscomp_len);
  munmap((void *) gammaptrs,gammaptrs_len);
#else
  FREE(offsetscomp);
  FREE(gammaptrs);
#endif

  return;
}


static Oligospace_T
power (int base, int exponent) {
#ifdef OLIGOSPACE_NOT_LONG
  Oligospace_T result = 1U;
#else
  Oligospace_T result = 1UL;
#endif
  int i;

  for (i = 0; i < exponent; i++) {
    result *= base;
  }
  return result;
}


void
Indexdb_write_offsets (char *gammaptrsfile, char *offsetscompfile, FILE *sequence_fp, Univ_IIT_T chromosome_iit,
		       int offsetscomp_basesize,
#ifdef PMAP
		       int alphabet_size, int index1part_aa, bool watsonp,
#else
		       int index1part,
#endif
		       int index1interval, bool genome_lc_p, char *fileroot, bool mask_lowercase_p) {
  char *uppercaseCode;

  /* If offsets[oligospace] > 2^32, then will will want to allocate and write 8-mers for offsets file */
  Positionsptr_T *offsets;

  char *comma;
  int c, nchrs, chrnum;
  Oligospace_T oligospace, oligoi;
  Univcoord_T position = 0, next_chrbound;
  Chrpos_T chrpos = 0U;
#ifdef PMAP
  int frame = -1, between_counter[3], in_counter[3];
  Storedoligomer_T high = 0U, low = 0U, carry;
  Storedoligomer_T aaindex;
  int index1part_nt = 3*index1part_aa;
#else
  int between_counter = 0, in_counter = 0;
  Storedoligomer_T oligo = 0U, masked, mask;
#endif
#ifdef DEBUG1
  char *aa;
#endif

  int offsetscomp_blocksize;
  int circular_typeint;

  if (mask_lowercase_p == false) {
    uppercaseCode = UPPERCASE_U2T; /* We are reading DNA sequence */
  } else {
    uppercaseCode = NO_UPPERCASE;
  }

#ifdef PMAP
  oligospace = power(alphabet_size,index1part_aa);
  between_counter[0] = between_counter[1] = between_counter[2] = 0;
  in_counter[0] = in_counter[1] = in_counter[2] = 0;
#ifdef OLIGOSPACE_NOT_LONG
  fprintf(stderr,"Allocating %u*%lu bytes for offsets\n",oligospace+1U,sizeof(Positionsptr_T));
#else
  fprintf(stderr,"Allocating %lu*%lu bytes for offsets\n",oligospace+1UL,sizeof(Positionsptr_T));
#endif
  offsets = (Positionsptr_T *) CALLOC_NO_EXCEPTION(oligospace+1,sizeof(Positionsptr_T));
  if (offsets == NULL) {
#ifdef OLIGOSPACE_NOT_LONG
    fprintf(stderr,"Unable to allocate %u bytes of memory, needed to build offsets with %d-mers\n",oligospace+1U,index1part_aa);
#else
    fprintf(stderr,"Unable to allocate %lu bytes of memory, needed to build offsets with %d-mers\n",oligospace+1UL,index1part_aa);
#endif
    fprintf(stderr,"Either find a computer with more RAM, or lower your value for the k-mer size\n");
    exit(9);
  }
  offsetscomp_blocksize = power(alphabet_size,index1part_aa - offsetscomp_basesize);
#else
  mask = ~(~0UL << 2*index1part);
  oligospace = power(4,index1part);
#ifdef OLIGOSPACE_NOT_LONG
  fprintf(stderr,"Allocating %u*%lu bytes for offsets\n",oligospace+1U,sizeof(Positionsptr_T));
#else
  fprintf(stderr,"Allocating %lu*%lu bytes for offsets\n",oligospace+1UL,sizeof(Positionsptr_T));
#endif
  offsets = (Positionsptr_T *) CALLOC_NO_EXCEPTION(oligospace+1,sizeof(Positionsptr_T));
  if (offsets == NULL) {
#ifdef OLIGOSPACE_NOT_LONG
    fprintf(stderr,"Unable to allocate %u bytes of memory, needed to build offsets with %d-mers\n",oligospace+1U,index1part);
#else
    fprintf(stderr,"Unable to allocate %lu bytes of memory, needed to build offsets with %d-mers\n",oligospace+1UL,index1part);
#endif
    fprintf(stderr,"Either find a computer with more RAM, or lower your value for the k-mer size\n");
    exit(9);
  }
  offsetscomp_blocksize = power(4,index1part - offsetscomp_basesize);
#endif

  /* Handle reference strain */
  circular_typeint = Univ_IIT_typeint(chromosome_iit,"circular");
  chrnum = 1;
  nchrs = Univ_IIT_total_nintervals(chromosome_iit);
  next_chrbound = Univ_IIT_next_chrbound(chromosome_iit,chrnum,circular_typeint);

  while ((c = Compress_get_char(sequence_fp,position,genome_lc_p)) != EOF) {
#ifdef PMAP
    if (++frame == 3) {
      frame = 0;
    }
    between_counter[frame] += 1;
    in_counter[frame] += 1;
#else
    between_counter++;
    in_counter++;
#endif

    if (position % MONITOR_INTERVAL == 0) {
      comma = Genomicpos_commafmt(position);
#ifdef PMAP
      fprintf(stderr,"Indexing offsets of oligomers in genome %s (%d aa every %d aa), position %s",
	      fileroot,index1part_aa,index1interval,comma);
#else
      fprintf(stderr,"Indexing offsets of oligomers in genome %s (%d bp every %d bp), position %s",
	      fileroot,index1part,index1interval,comma);
#endif
      FREE(comma);
#ifdef PMAP
      if (watsonp == true) {
	fprintf(stderr," (fwd)");
      } else {
	fprintf(stderr," (rev)");
      }
#endif
      fprintf(stderr,"\n");
    }

#ifdef PMAP
    carry = (low >> 30);
    switch (uppercaseCode[c]) {
    case 'A': low = (low << 2); break;
    case 'C': low = (low << 2) | 1U; break;
    case 'G': low = (low << 2) | 2U; break;
    case 'T': low = (low << 2) | 3U; break;
    case 'X': case 'N': 
      high = low = carry = 0U; 
      in_counter[0] = in_counter[1] = in_counter[2] = 0;
      break;
    default: 
      if (genome_lc_p == true) {
	high = low = carry = 0U;
	in_counter[0] = in_counter[1] = in_counter[2] = 0;
      } else {
	fprintf(stderr,"Bad character %c at position %u\n",c,position);
	abort();
      }
    }
    high = (high << 2) | carry; 
#else
    switch (uppercaseCode[c]) {
    case 'A': oligo = (oligo << 2); break;
    case 'C': oligo = (oligo << 2) | 1U; break;
    case 'G': oligo = (oligo << 2) | 2U; break;
    case 'T': oligo = (oligo << 2) | 3U; break;
    case 'X': case 'N': oligo = 0U; in_counter = 0; break;
    default: 
      if (genome_lc_p == true) {
	oligo = 0U; in_counter = 0;
      } else {
	fprintf(stderr,"Bad character %c at position %lu\n",c,position);
	abort();
      }
    }
#endif

#ifdef PMAP
    debug(printf("frame=%d char=%c bc=%d ic=%d high=%08X low=%08X\n",
		 frame,c,between_counter[frame],in_counter[frame],high,low));

    if (in_counter[frame] > 0) {
      if (watsonp == true) {
	if (Alphabet_get_codon_fwd(low) == AA_STOP) {
	  debug(printf("Resetting in_counter for frame %d to 0\n",frame));
	  in_counter[frame] = 0; 
	}
      } else {
	if (Alphabet_get_codon_rev(low) == AA_STOP) {
	  debug(printf("Resetting in_counter for frame %d to 0\n",frame));
	  in_counter[frame] = 0; 
	}
      }
    }
    if (in_counter[frame] == index1part_aa + 1) {
      if (between_counter[frame] >= index1interval) {
	aaindex = Alphabet_get_aa_index(high,low,watsonp,index1part_nt);
#ifdef OLIGOSPACE_NOT_LONG
	oligoi = (Oligospace_T) aaindex + 1U;
#else
	oligoi = (Oligospace_T) aaindex + 1UL;
#endif
	offsets[oligoi] += 1;
	debug1(
	       aa = aaindex_aa(aaindex);
	       if (watsonp == true) {
		 printf("Storing %s (%u) at %u\n",aa,aaindex,position-index1part_nt+1U);
	       } else {
		 printf("Storing %s (%u) at %u\n",aa,aaindex,position);
	       }
	       FREE(aa);
	       );
	between_counter[frame] = 0;
      }
      in_counter[frame] -= 1;
    }
#else
    if (in_counter == index1part) {
      if (
#ifdef NONMODULAR
	  between_counter >= index1interval
#else
	  (chrpos-index1part+1U) % index1interval == 0
#endif
	  ) {
	masked = oligo & mask;
#ifdef OLIGOSPACE_NOT_LONG
	oligoi = (Oligospace_T) masked + 1U;
#else
	oligoi = (Oligospace_T) masked + 1UL;
#endif
	offsets[oligoi] += 1;
	debug(printf("Found oligo %06X.  Incremented offsets for %lu to be %u\n",
		     masked,oligoi,offsets[oligoi]));
	between_counter = 0;
      }
      in_counter--;
    }
#endif

    chrpos++;			/* Needs to go here, before we reset chrpos to 0 */
    if (position >= next_chrbound) {
#ifdef PMAP
      high = low = carry = 0U;
      in_counter[0] = in_counter[1] = in_counter[2] = 0;
#else
      oligo = 0U; in_counter = 0;
#endif
      chrpos = 0U;
      chrnum++;
      while (chrnum <= nchrs && (next_chrbound = Univ_IIT_next_chrbound(chromosome_iit,chrnum,circular_typeint)) < position) {
	chrnum++;
      }
    }
    position++;
  }


#ifdef ADDSENTINEL
  for (oligoi = 1; oligoi <= oligospace; oligoi++) {
    offsets[oligoi] = offsets[oligoi] + offsets[oligoi-1] + 1U;
    debug(if (offsets[oligoi] != offsets[oligoi-1]) {
	    printf("Offset for %06X: %u\n",oligoi,offsets[oligoi]);
	  });
  }
#else
  for (oligoi = 1; oligoi <= oligospace; oligoi++) {
    offsets[oligoi] = offsets[oligoi] + offsets[oligoi-1];
    debug(if (offsets[oligoi] != offsets[oligoi-1]) {
	    printf("Offset for %06X: %u\n",oligoi,offsets[oligoi]);
	  });
  }
#endif

  /*
  fprintf(stderr,"Offset for A...A is %u to %u\n",offsets[0],offsets[1]);
  fprintf(stderr,"Offset for T...T is %u to %u\n",offsets[oligospace-1],offsets[oligospace]);
  */

#ifdef PRE_GAMMAS
  fprintf(stderr,"Writing %lu offsets to file with total of %u positions...",oligospace+1,offsets[oligospace]);
  FWRITE_UINTS(offsets,oligospace+1,offsets_fp);
#else
#ifdef OLIGOSPACE_NOT_LONG
  fprintf(stderr,"Writing %u offsets compressed to file with total of %u positions...",oligospace+1U,offsets[oligospace]);
#else
  fprintf(stderr,"Writing %lu offsets compressed to file with total of %u positions...",oligospace+1UL,offsets[oligospace]);
#endif
  Indexdb_write_gammaptrs(gammaptrsfile,offsetscompfile,offsets,oligospace,offsetscomp_blocksize);
#endif
  fprintf(stderr,"done\n");

  
  if (offsetscomp_blocksize > 1) {
    fprintf(stderr,"Checking gammas...");
    check_offsets_from_gammas(gammaptrsfile,offsetscompfile,offsets,oligospace,offsetscomp_blocksize);
    fprintf(stderr,"done\n");
  }

  FREE(offsets);

  return;
}


/* FILE *fp is preferable to int fd, because former is buffered.  No
   need for fseeko, because offsets file is < 2 Gigabytes */
#if 0
static void
offsetsfile_move_absolute (FILE *fp, int ptr) {
  long int offset = ptr*((long int) sizeof(Positionsptr_T));

  if (fseek(fp,offset,SEEK_SET) < 0) {
    fprintf(stderr,"Attempted to do fseek on offset %u*%lu=%lu\n",ptr,sizeof(Positionsptr_T),offset);
    perror("Error in indexdb.c, offsetsfile_move_absolute");
    exit(9);
  }

  return;
}
#endif


#if 0
static bool
need_to_sort_p (Univcoord_T *positions, int length) {
  Univcoord_T prevpos;
  int j;

  prevpos = positions[0];
  for (j = 1; j < length; j++) {
    if (positions[j] <= prevpos) {
      return true;
    } else {
      prevpos = positions[j];
    }
  }
  return false;
}
#endif


static void
positions_move_absolute_4 (int positions_fd, Positionsptr_T ptr) {
  off_t offset = ptr*((off_t) sizeof(UINT4));

  if (lseek(positions_fd,offset,SEEK_SET) < 0) {
    fprintf(stderr,"Attempted to do lseek on offset %u*%lu=%lu\n",ptr,sizeof(UINT4),offset);
    perror("Error in indexdb.c, positions_move_absolute_4");
    exit(9);
  }
  return;
}

static void
positions_move_absolute_8 (int positions_fd, Positionsptr_T ptr) {
  off_t offset = ptr*((off_t) sizeof(UINT8));

  if (lseek(positions_fd,offset,SEEK_SET) < 0) {
    fprintf(stderr,"Attempted to do lseek on offset %u*%lu=%lu\n",ptr,sizeof(UINT8),offset);
    perror("Error in indexdb.c, positions_move_absolute_8");
    exit(9);
  }
  return;
}


/* Works directly in file, so we don't need to allocate memory */
static void
compute_positions_in_file (int positions_fd, Positionsptr_T *offsets,
			   FILE *sequence_fp, Univ_IIT_T chromosome_iit,
#ifdef PMAP
			   int index1part_aa, bool watsonp,
#else
			   int index1part,
#endif
			   int index1interval, bool genome_lc_p, char *fileroot,
			   bool mask_lowercase_p, bool coord_values_8p) {
  char *uppercaseCode;
  Univcoord_T position = 0, next_chrbound;
  UINT8 adjposition8;
  UINT4 adjposition4;
  Chrpos_T chrpos = 0U;
  char *comma;
  int c, nchrs, chrnum;
#ifdef PMAP
  int frame = -1, between_counter[3], in_counter[3];
  Storedoligomer_T high = 0U, low = 0U, carry;
  Oligospace_T aaindex;
  int index1part_nt = 3*index1part_aa;
#else
  int between_counter = 0, in_counter = 0;
  Storedoligomer_T oligo = 0U, masked, mask;
#endif
  int circular_typeint;

#ifdef ADDSENTINEL
  Oligospace_T oligospace, oligoi;
  oligospace = power(4,index1part);
#endif

  if (mask_lowercase_p == false) {
    uppercaseCode = UPPERCASE_U2T; /* We are reading DNA sequence */
  } else {
    uppercaseCode = NO_UPPERCASE;
  }

#ifdef PMAP
  /* oligospace = power(alphabet_size,index1part_aa); */
#else
  mask = ~(~0UL << 2*index1part);
  /* oligospace = power(4,index1part); */
#endif

  /* Handle reference strain */
  circular_typeint = Univ_IIT_typeint(chromosome_iit,"circular");
  chrnum = 1;
  nchrs = Univ_IIT_total_nintervals(chromosome_iit);
  next_chrbound = Univ_IIT_next_chrbound(chromosome_iit,chrnum,circular_typeint);

  while ((c = Compress_get_char(sequence_fp,position,genome_lc_p)) != EOF) {
#ifdef PMAP
    if (++frame == 3) {
      frame = 0;
    }
    between_counter[frame] += 1;
    in_counter[frame] += 1;
#else
    between_counter++;
    in_counter++;
#endif

    if (position % MONITOR_INTERVAL == 0) {
      comma = Genomicpos_commafmt(position);
#ifdef PMAP
      fprintf(stderr,"Indexing positions of oligomers in genome %s (%d aa every %d aa), position %s",
	      fileroot,index1part_aa,index1interval,comma);
#else
      fprintf(stderr,"Indexing positions of oligomers in genome %s (%d bp every %d bp), position %s",
	      fileroot,index1part,index1interval,comma);
#endif
      FREE(comma);
#ifdef PMAP
      if (watsonp == true) {
	fprintf(stderr," (fwd)");
      } else {
	fprintf(stderr," (rev)");
      }
#endif
      fprintf(stderr,"\n");
    }

#ifdef PMAP
    carry = (low >> 30);
    switch (uppercaseCode[c]) {
    case 'A': low = (low << 2); break;
    case 'C': low = (low << 2) | 1; break;
    case 'G': low = (low << 2) | 2; break;
    case 'T': low = (low << 2) | 3; break;
    case 'X': case 'N': 
      high = low = carry = 0U;
      in_counter[0] = in_counter[1] = in_counter[2] = 0;
      break;
    default: 
      if (genome_lc_p == true) {
	high = low = carry = 0U;
	in_counter[0] = in_counter[1] = in_counter[2] = 0;
      } else {
	fprintf(stderr,"Bad character %c at position %u\n",c,position);
	abort();
      }
    }
    high = (high << 2) | carry; 
#else
    switch (uppercaseCode[c]) {
    case 'A': oligo = (oligo << 2); break;
    case 'C': oligo = (oligo << 2) | 1; break;
    case 'G': oligo = (oligo << 2) | 2; break;
    case 'T': oligo = (oligo << 2) | 3; break;
    case 'X': case 'N': oligo = 0U; in_counter = 0; break;
    default: 
      if (genome_lc_p == true) {
	oligo = 0U; in_counter = 0;
      } else {
	fprintf(stderr,"Bad character %c at position %lu\n",c,position);
	abort();
      }
    }
#endif

    /*
    debug(printf("char=%c bc=%d ic=%d oligo=%016lX\n",
		 c,between_counter,in_counter,oligo));
    */
    
#ifdef PMAP
    if (in_counter[frame] > 0) {
      if (watsonp == true) {
	if (Alphabet_get_codon_fwd(low) == AA_STOP) {
	  in_counter[frame] = 0; 
	}
      } else {
	if (Alphabet_get_codon_rev(low) == AA_STOP) {
	  in_counter[frame] = 0; 
	}
      }
    }
    if (in_counter[frame] == index1part_aa + 1) {
      if (between_counter[frame] >= index1interval) {
	aaindex = Alphabet_get_aa_index(high,low,watsonp,index1part_nt);
	if (coord_values_8p == true) {
	  positions_move_absolute_8(positions_fd,offsets[aaindex]);
	  offsets[aaindex] += 1;
	  if (watsonp == true) {
	    adjposition8 = position-index1part_nt+1U;
	  } else {
	    adjposition8 = position;
	  }
	  WRITE_UINT8(adjposition8,positions_fd);
	} else {
	  positions_move_absolute_4(positions_fd,offsets[aaindex]);
	  offsets[aaindex] += 1;
	  if (watsonp == true) {
	    adjposition4 = (UINT4) (position-index1part_nt+1U);
	  } else {
	    adjposition4 = (UINT4) position;
	  }
	  WRITE_UINT(adjposition4,positions_fd);
	}
	between_counter[frame] = 0;
      }
      in_counter[frame] -= 1;
    }
#else
    if (in_counter == index1part) {
      if (
#ifdef NONMODULAR
	  between_counter >= index1interval
#else
	  (chrpos-index1part+1U) % index1interval == 0
#endif
	  ) {
	masked = oligo & mask;
	if (coord_values_8p == true) {
	  positions_move_absolute_8(positions_fd,offsets[masked]);
	  offsets[masked] += 1;
	  adjposition8 = position-index1part+1U;
	  WRITE_UINT8(adjposition8,positions_fd);
	} else {
	  positions_move_absolute_4(positions_fd,offsets[masked]);
	  offsets[masked] += 1;
	  adjposition4 = (UINT4) (position-index1part+1U);
	  WRITE_UINT(adjposition4,positions_fd);
	}
	between_counter = 0;
      }
      in_counter--;
    }
#endif
    
    chrpos++;			/* Needs to go here, before we reset chrpos to 0 */
    if (position >= next_chrbound) {
#ifdef PMAP
      high = low = carry = 0U;
      in_counter[0] = in_counter[1] = in_counter[2] = 0;
#else
      oligo = 0U; in_counter = 0;
#endif
      chrpos = 0U;
      chrnum++;
      while (chrnum <= nchrs && (next_chrbound = Univ_IIT_next_chrbound(chromosome_iit,chrnum,circular_typeint)) < position) {
	chrnum++;
      }
    }
    position++;
  }

#ifdef ADDSENTINEL
  if (coord_values_8p == true) {
    for (oligoi = 0; oligoi < oligospace; oligoi++) {
      positions_move_absolute_8(positions_fd,offsets[oligoi]);
      WRITE_UINT8(-1UL,positions_fd);
    }
  } else {
    for (oligoi = 0; oligoi < oligospace; oligoi++) {
      positions_move_absolute_4(positions_fd,offsets[oligoi]);
      WRITE_UINT(-1U,positions_fd);
    }
  }
#endif

  return;
}

/* Requires sufficient memory to hold all positions */
static void
compute_positions_in_memory (UINT4 *positions4, UINT8 *positions8, Positionsptr_T *offsets,
			     FILE *sequence_fp, Univ_IIT_T chromosome_iit,
#ifdef PMAP
			     int index1part_aa, bool watsonp,
#else
			     int index1part,
#endif
			     int index1interval, bool genome_lc_p, char *fileroot, bool mask_lowercase_p,
			     bool coord_values_8p) {
  char *uppercaseCode;
  Univcoord_T position = 0, next_chrbound;
  Chrpos_T chrpos = 0U;
  char *comma;
  int c, nchrs, chrnum;
#ifdef PMAP
  int frame = -1, between_counter[3], in_counter[3];
  Storedoligomer_T high = 0U, low = 0U, carry;
  Storedoligomer_T aaindex;
  int index1part_nt = 3*index1part_aa;
  debug1(char *aa);
#else
  int between_counter = 0, in_counter = 0;
  Storedoligomer_T oligo = 0U, masked, mask;
  debug1(char *nt);
#endif
  int circular_typeint;

#ifdef ADDSENTINEL
  Oligospace_T oligospace, oligoi;
  oligospace = power(4,index1part);
#endif

  if (mask_lowercase_p == false) {
    uppercaseCode = UPPERCASE_U2T; /* We are reading DNA sequence */
  } else {
    uppercaseCode = NO_UPPERCASE;
  }

#ifdef PMAP
  between_counter[0] = between_counter[1] = between_counter[2] = 0;
  in_counter[0] = in_counter[1] = in_counter[2] = 0;
#else
  mask = ~(~0UL << 2*index1part);
#endif

  /* Handle reference strain */
  circular_typeint = Univ_IIT_typeint(chromosome_iit,"circular");
  chrnum = 1;
  nchrs = Univ_IIT_total_nintervals(chromosome_iit);
  next_chrbound = Univ_IIT_next_chrbound(chromosome_iit,chrnum,circular_typeint);

  while ((c = Compress_get_char(sequence_fp,position,genome_lc_p)) != EOF) {
#ifdef PMAP
    if (++frame == 3) {
      frame = 0;
    }
    between_counter[frame] += 1;
    in_counter[frame] += 1;
#else
    between_counter++;
    in_counter++;
#endif

    if (position % MONITOR_INTERVAL == 0) {
      comma = Genomicpos_commafmt(position);
#ifdef PMAP
      fprintf(stderr,"Indexing positions of oligomers in genome %s (%d aa every %d aa), position %s",
	      fileroot,index1part_aa,index1interval,comma);
#else
      fprintf(stderr,"Indexing positions of oligomers in genome %s (%d bp every %d bp), position %s",
	      fileroot,index1part,index1interval,comma);
#endif
      FREE(comma);
#ifdef PMAP
      if (watsonp == true) {
	fprintf(stderr," (fwd)");
      } else {
	fprintf(stderr," (rev)");
      }
#endif
      fprintf(stderr,"\n");
    }

#ifdef PMAP
    carry = (low >> 30);
    switch (uppercaseCode[c]) {
    case 'A': low = (low << 2); break;
    case 'C': low = (low << 2) | 1U; break;
    case 'G': low = (low << 2) | 2U; break;
    case 'T': low = (low << 2) | 3U; break;
    case 'X': case 'N':
      high = low = carry = 0U;
      in_counter[0] = in_counter[1] = in_counter[2] = 0;
      break;
    default: 
      if (genome_lc_p == true) {
	high = low = carry = 0U;
	in_counter[0] = in_counter[1] = in_counter[2] = 0;
      } else {
	fprintf(stderr,"Bad character %c at position %u\n",c,position);
	abort();
      }
    }
    high = (high << 2) | carry; 

    debug(printf("frame=%d char=%c bc=%d ic=%d high=%08X low=%08X\n",
		 frame,c,between_counter[frame],in_counter[frame],high,low));
#else
    switch (uppercaseCode[c]) {
    case 'A': oligo = (oligo << 2); break;
    case 'C': oligo = (oligo << 2) | 1U; break;
    case 'G': oligo = (oligo << 2) | 2U; break;
    case 'T': oligo = (oligo << 2) | 3U; break;
    case 'X': case 'N': oligo = 0U; in_counter = 0; break;
    default: 
      if (genome_lc_p == true) {
	oligo = 0U; in_counter = 0;
      } else {
	fprintf(stderr,"Bad character %c at position %lu\n",c,position);
	abort();
      }
    }

    debug(printf("char=%c bc=%d ic=%d oligo=%08X\n",
		 c,between_counter,in_counter,oligo));
#endif

    
#ifdef PMAP
    if (in_counter[frame] > 0) {
      if (watsonp == true) {
	if (Alphabet_get_codon_fwd(low) == AA_STOP) {
	  in_counter[frame] = 0; 
	}
      } else {
	if (Alphabet_get_codon_rev(low) == AA_STOP) {
	  in_counter[frame] = 0; 
	}
      }
    }
    if (in_counter[frame] == index1part_aa + 1) {
      if (between_counter[frame] >= index1interval) {
	aaindex = Alphabet_get_aa_index(high,low,watsonp,index1part_nt);
	if (coord_values_8p == true) {
	  if (watsonp == true) {
	    positions8[offsets[aaindex]++] = position-index1part_nt+1UL;
	    debug1(adjposition = position-index1part_nt+1UL);
	  } else {
	    positions8[offsets[aaindex]++] = position;
	    debug1(adjposition = position);
	  }
	} else {
	  if (watsonp == true) {
	    positions4[offsets[aaindex]++] = (UINT4) (position-index1part_nt+1U);
	    debug1(adjposition = position-index1part_nt+1U);
	  } else {
	    positions4[offsets[aaindex]++] = (UINT4) position;
	    debug1(adjposition = position);
	  }
	}
	debug1(
	       aa = aaindex_aa(aaindex);
	       printf("Storing %s (%u) at %u\n",aa,aaindex,adjposition);
	       FREE(aa);
	       );
	between_counter[frame] = 0;
      }
      in_counter[frame] -= 1;
    }
#else
    if (in_counter == index1part) {
      if (
#ifdef NONMODULAR
	  between_counter >= index1interval
#else
	  (chrpos-index1part+1U) % index1interval == 0
#endif
	  ) {
	masked = oligo & mask;
#if 0
	if (offsets[masked] >= totalcounts) {
	  fprintf(stderr,"masked = %u, offsets[masked] = %u >= totalcounts %u\n",
		  masked,offsets[masked],totalcounts);
	  exit(9);
	}
#endif
	if (coord_values_8p == true) {
	  positions8[offsets[masked]++] = position-index1part+1U;
	} else {
	  positions4[offsets[masked]++] = (UINT4) (position-index1part+1U);
	}
	debug1(nt = shortoligo_nt(masked,index1part);
	       printf("Storing %s at %lu, chrpos %u\n",nt,position-index1part+1U,chrpos-index1part+1U);
	       FREE(nt));
	between_counter = 0;
      }
      in_counter--;
    }
#endif
    
    chrpos++;			/* Needs to go here, before we reset chrpos to 0 */
    if (position >= next_chrbound) {
      debug1(printf("Skipping because position %u is at chrbound\n",position));
#ifdef PMAP
      high = low = carry = 0U;
      in_counter[0] = in_counter[1] = in_counter[2] = 0;
#else
      oligo = 0U; in_counter = 0;
#endif
      chrpos = 0U;
      chrnum++;
      while (chrnum <= nchrs && (next_chrbound = Univ_IIT_next_chrbound(chromosome_iit,chrnum,circular_typeint)) < position) {
	chrnum++;
      }
    }
    position++;
  }

#ifdef ADDSENTINEL
  if (coord_values_8p == true) {
    for (oligoi = 0; oligoi < oligospace; oligoi++) {
      positions8[offsets[oligoi]] = -1UL;
    }
  } else {
    for (oligoi = 0; oligoi < oligospace; oligoi++) {
      positions4[offsets[oligoi]] = (UINT4) -1U;
    }
  }
  /*
  fprintf(stderr,"Put a sentinel at %u\n",offsets[0]);
  fprintf(stderr,"Put a sentinel at %u\n",offsets[oligospace-2]);
  fprintf(stderr,"Put a sentinel at %u\n",offsets[oligospace-1]);
  */
#endif

  return;
}


#define WRITE_CHUNK 1000000

void
Indexdb_write_positions (char *positionsfile, char *gammaptrsfile, char *offsetscompfile,
			 FILE *sequence_fp, Univ_IIT_T chromosome_iit, int offsetscomp_basesize,
#ifdef PMAP
			 int alphabet_size, int index1part_aa, bool watsonp,
#else
			 int index1part,
#endif
			 int index1interval, bool genome_lc_p, bool writefilep,
			 char *fileroot, bool mask_lowercase_p, bool coord_values_8p) {
  FILE *positions_fp;		/* For building positions in memory */
  int positions_fd;		/* For building positions in file */
  Positionsptr_T *offsets = NULL, totalcounts, count;
  UINT4 *positions4;
  UINT8 *positions8;
  Oligospace_T oligospace;
  off_t filesize;

#ifdef PMAP
  offsets = Indexdb_offsets_from_gammas(gammaptrsfile,offsetscompfile,offsetscomp_basesize,
					alphabet_size,index1part_aa);
  oligospace = power(alphabet_size,index1part_aa);
#else
  offsets = Indexdb_offsets_from_gammas(gammaptrsfile,offsetscompfile,offsetscomp_basesize,index1part);
  oligospace = power(4,index1part);
#endif
  totalcounts = offsets[oligospace];
  if (totalcounts == 0) {
    fprintf(stderr,"Something is wrong with the offsets file.  Total counts is zero.\n");
    exit(9);
  }

  if (writefilep == true) {
    fprintf(stderr,"User requested build of positions in file\n");
    positions_fd = Access_fileio_rw(positionsfile);
#ifdef PMAP
    compute_positions_in_file(positions_fd,offsets,sequence_fp,chromosome_iit,
			      index1part_aa,watsonp,index1interval,genome_lc_p,fileroot,mask_lowercase_p,
			      coord_values_8p);
#else
    compute_positions_in_file(positions_fd,offsets,sequence_fp,chromosome_iit,
			      index1part,index1interval,genome_lc_p,fileroot,mask_lowercase_p,
			      coord_values_8p);
#endif
    close(positions_fd);

  } else if (coord_values_8p == true) {
    fprintf(stderr,"Trying to allocate %u*%d bytes of memory...",totalcounts,(int) sizeof(UINT8));
    positions8 = (UINT8 *) CALLOC_NO_EXCEPTION(totalcounts,sizeof(UINT8));
    if (positions8 == NULL) {
      fprintf(stderr,"failed.  Building positions in file.\n");
      positions_fd = Access_fileio_rw(positionsfile);
#ifdef PMAP
      compute_positions_in_file(positions_fd,offsets,sequence_fp,chromosome_iit,
				index1part_aa,watsonp,index1interval,genome_lc_p,fileroot,
				mask_lowercase_p,/*coord_values_8p*/true);
#else
      compute_positions_in_file(positions_fd,offsets,sequence_fp,chromosome_iit,
				index1part,index1interval,genome_lc_p,fileroot,
				mask_lowercase_p,/*coord_values_8p*/true);
#endif
      close(positions_fd);

      if ((filesize = Access_filesize(positionsfile)) != totalcounts * (off_t) sizeof(UINT8)) {
	fprintf(stderr,"Error after file-based build: expected file size for %s is %lu, but observed only %lu.  Please notify twu@gene.com of this error.\n",
		positionsfile,totalcounts*sizeof(UINT8),filesize);
	abort();
      }

    } else {
      fprintf(stderr,"succeeded.  Building positions in memory.\n");
      if ((positions_fp = FOPEN_WRITE_BINARY(positionsfile)) == NULL) {
	fprintf(stderr,"Can't open file %s\n",positionsfile);
	exit(9);
      }
#ifdef PMAP
      compute_positions_in_memory(/*positions4*/NULL,positions8,offsets,sequence_fp,chromosome_iit,
				  index1part_aa,watsonp,index1interval,genome_lc_p,fileroot,
				  mask_lowercase_p,/*coord_values_8p*/true);
#else
      compute_positions_in_memory(/*positions4*/NULL,positions8,offsets,sequence_fp,chromosome_iit,
				  index1part,index1interval,genome_lc_p,fileroot,
				  mask_lowercase_p,/*coord_values_8p*/true);
#endif
      fprintf(stderr,"Writing %u genomic positions to file %s ...\n",
	      totalcounts,positionsfile);
      FWRITE_UINT8S(positions8,totalcounts,positions_fp);

      fclose(positions_fp);

      if ((filesize = Access_filesize(positionsfile)) != totalcounts * (off_t) sizeof(UINT8)) {
	fprintf(stderr,"Error: expected file size for %s is %lu, but observed only %lu.  Trying now to write with smaller chunks.\n",
		positionsfile,totalcounts*sizeof(UINT8),filesize);
	if ((positions_fp = FOPEN_WRITE_BINARY(positionsfile)) == NULL) {
	  fprintf(stderr,"Can't open file %s\n",positionsfile);
	  exit(9);
	}
	for (count = 0; count + WRITE_CHUNK < totalcounts; count += WRITE_CHUNK) {
	  FWRITE_UINT8S(&(positions8[count]),count,positions_fp);
	}
	if (count < totalcounts) {
	  FWRITE_UINT8S(&(positions8[count]),totalcounts - count,positions_fp);
	}
	fclose(positions_fp);

	if ((filesize = Access_filesize(positionsfile)) != totalcounts * (off_t) sizeof(UINT8)) {
	  fprintf(stderr,"Error persists: expected file size for %s is %lu, but observed only %lu.  Please notify twu@gene.com of this error.\n",
		  positionsfile,totalcounts*sizeof(UINT8),filesize);
	  abort();
	}
      }

      FREE(positions8);
    }

  } else {
    fprintf(stderr,"Trying to allocate %u*%d bytes of memory...",totalcounts,(int) sizeof(UINT4));
    positions4 = (UINT4 *) CALLOC_NO_EXCEPTION(totalcounts,sizeof(UINT4));
    if (positions4 == NULL) {
      fprintf(stderr,"failed.  Building positions in file.\n");
      positions_fd = Access_fileio_rw(positionsfile);
#ifdef PMAP
      compute_positions_in_file(positions_fd,offsets,sequence_fp,chromosome_iit,
				index1part_aa,watsonp,index1interval,genome_lc_p,fileroot,mask_lowercase_p,
				/*coord_values_8*/false);
#else
      compute_positions_in_file(positions_fd,offsets,sequence_fp,chromosome_iit,
				index1part,index1interval,genome_lc_p,fileroot,mask_lowercase_p,
				/*coord_values_8p*/false);
#endif
      close(positions_fd);

      if ((filesize = Access_filesize(positionsfile)) != totalcounts * (off_t) sizeof(UINT4)) {
	fprintf(stderr,"Error after file-based build: expected file size for %s is %lu, but observed only %lu.  Please notify twu@gene.com of this error.\n",
		positionsfile,totalcounts*sizeof(UINT4),filesize);
	abort();
      }

    } else {
      fprintf(stderr,"succeeded.  Building positions in memory.\n");
      if ((positions_fp = FOPEN_WRITE_BINARY(positionsfile)) == NULL) {
	fprintf(stderr,"Can't open file %s\n",positionsfile);
	exit(9);
      }
#ifdef PMAP
      compute_positions_in_memory(positions4,/*positions8*/NULL,offsets,sequence_fp,chromosome_iit,
				  index1part_aa,watsonp,index1interval,genome_lc_p,fileroot,
				  mask_lowercase_p,/*coord_values_8p*/false);
#else
      compute_positions_in_memory(positions4,/*positions8*/NULL,offsets,sequence_fp,chromosome_iit,
				  index1part,index1interval,genome_lc_p,fileroot,
				  mask_lowercase_p,/*coord_values_8p*/false);
#endif
      fprintf(stderr,"Writing %u genomic positions to file %s ...\n",
	      totalcounts,positionsfile);
      FWRITE_UINTS(positions4,totalcounts,positions_fp);

      fclose(positions_fp);

      if ((filesize = Access_filesize(positionsfile)) != totalcounts * (off_t) sizeof(UINT4)) {
	fprintf(stderr,"Error: expected file size for %s is %lu, but observed only %lu.  Trying now to write with smaller chunks.\n",
		positionsfile,totalcounts*sizeof(UINT4),filesize);
	if ((positions_fp = FOPEN_WRITE_BINARY(positionsfile)) == NULL) {
	  fprintf(stderr,"Can't open file %s\n",positionsfile);
	  exit(9);
	}
	for (count = 0; count + WRITE_CHUNK < totalcounts; count += WRITE_CHUNK) {
	  FWRITE_UINTS(&(positions4[count]),count,positions_fp);
	}
	if (count < totalcounts) {
	  FWRITE_UINTS(&(positions4[count]),totalcounts - count,positions_fp);
	}
	fclose(positions_fp);

	if ((filesize = Access_filesize(positionsfile)) != totalcounts * (off_t) sizeof(UINT4)) {
	  fprintf(stderr,"Error persists: expected file size for %s is %lu, but observed only %lu.  Please notify twu@gene.com of this error.\n",
		  positionsfile,totalcounts*sizeof(UINT4),filesize);
	  abort();
	}
      }

      FREE(positions4);
    }
  }

  FREE(offsets);

  return;
}



#if 0
void
Indexdb_convert_gammas (char *gammaptrsfile, char *offsetscompfile, FILE *offsets_fp,
#ifdef PMAP
			int alphabet_size, int index1part_aa,
#else
			int index1part,
#endif
			int blocksize) {
  int gammaptrs_fd, offsetscomp_fd;
  Positionsptr_T *offsets = NULL, totalcounts;
  int j;
  Oligospace_T oligospace, oligoi;

  UINT4 buffer;
  int ctr;
  UINT4 nwritten;

#ifdef PMAP
  oligospace = power(alphabet_size,index1part_aa);
#else
  oligospace = power(4,index1part);
#endif

  offsets = (Positionsptr_T *) CALLOC(oligospace+1,sizeof(Positionsptr_T));
  FREAD_UINTS(offsets,oligospace+1,offsets_fp);
  totalcounts = offsets[oligospace];
  if (totalcounts == 0) {
    fprintf(stderr,"Something is wrong with the offsets file.  Total counts is zero.\n");
    exit(9);
  }

  gammaptrs_fd = Access_fileio_rw(gammaptrsfile);
  offsetscomp_fd = Access_fileio_rw(offsetscompfile);

  nwritten = 0U;
  for (oligoi = 0; oligoi < oligospace; oligoi += blocksize) {
    WRITE_UINT(nwritten,gammaptrs_fd);

    WRITE_UINT(offsets[oligoi],offsetscomp_fd);
    nwritten += 1;

    buffer = 0U;
    ctr = 32;
    for (j = 1; j < blocksize; j++) {
      ctr = write_gamma(offsetscomp_fp,offsets_buffer,offsets_buffer_size,&offsets_buffer_i,
			&nwritten,&buffer,ctr,offsets[oligoi+j]-offsets[oligoi+j-1]);
    }
    debug(printf("writing gamma %08X\n",buffer));
    WRITE_UINT(buffer,offsetscomp_fd);
    nwritten += 1;
  }

  WRITE_UINT(offsets[oligoi],offsetscomp_fd);
  nwritten += 1;
  WRITE_UINT(nwritten,gammaptrs_fd);

  close(offsetscomp_fd);
  close(gammaptrs_fd);

  FREE(offsets);
  return;
}
#endif



