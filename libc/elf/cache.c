/* Copyright (C) 1999-2003,2005,2006,2007 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Andreas Jaeger <aj@suse.de>, 1999.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <errno.h>
#include <error.h>
#include <dirent.h>
#include <inttypes.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ldconfig.h>
#include <dl-cache.h>

struct cache_entry
{
  char *lib;			/* Library name.  */
  char *path;			/* Path to find library.  */
  int flags;			/* Flags to indicate kind of library.  */
  unsigned int osversion;	/* Required OS version.  */
  uint64_t hwcap;		/* Important hardware capabilities.  */
  int bits_hwcap;		/* Number of bits set in hwcap.  */
  struct cache_entry *next;	/* Next entry in list.  */
};

/* List of all cache entries.  */
static struct cache_entry *entries;

static const char *flag_descr[] =
{ "libc4", "ELF", "libc5", "libc6"};

/* Print a single entry.  */
static void
print_entry (const char *lib, int flag, unsigned int osversion,
	     uint64_t hwcap, const char *key)
{
  printf ("\t%s (", lib);
  switch (flag & FLAG_TYPE_MASK)
    {
    case FLAG_LIBC4:
    case FLAG_ELF:
    case FLAG_ELF_LIBC5:
    case FLAG_ELF_LIBC6:
      fputs (flag_descr[flag & FLAG_TYPE_MASK], stdout);
      break;
    default:
      fputs (_("unknown"), stdout);
      break;
    }
  switch (flag & FLAG_REQUIRED_MASK)
    {
    case FLAG_SPARC_LIB64:
      fputs (",64bit", stdout);
      break;
    case FLAG_IA64_LIB64:
      fputs (",IA-64", stdout);
      break;
    case FLAG_X8664_LIB64:
      fputs (",x86-64", stdout);
      break;
    case FLAG_S390_LIB64:
      fputs (",64bit", stdout);
      break;
    case FLAG_POWERPC_LIB64:
      fputs (",64bit", stdout);
      break;
    case FLAG_MIPS64_LIBN32:
      fputs (",N32", stdout);
      break;
    case FLAG_MIPS64_LIBN64:
      fputs (",64bit", stdout);
    case 0:
      break;
    default:
      printf (",%d", flag & FLAG_REQUIRED_MASK);
      break;
    }
  if (hwcap != 0)
    printf (", hwcap: %#.16" PRIx64, hwcap);
  if (osversion != 0)
    {
      static const char *const abi_tag_os[] =
      {
	[0] = "Linux",
	[1] = "Hurd",
	[2] = "Solaris",
	[3] = "FreeBSD",
	[4] = "kNetBSD",
	[5] = "Syllable",
	[6] = N_("Unknown OS")
      };
#define MAXTAG (sizeof abi_tag_os / sizeof abi_tag_os[0] - 1)
      unsigned int os = osversion >> 24;

      printf (_(", OS ABI: %s %d.%d.%d"),
	      _(abi_tag_os[os > MAXTAG ? MAXTAG : os]),
	      (osversion >> 16) & 0xff,
	      (osversion >> 8) & 0xff,
	      osversion & 0xff);
    }
  printf (") => %s\n", key);
}


/* Print the whole cache file, if a file contains the new cache format
   hidden in the old one, print the contents of the new format.  */
void
print_cache (const char *cache_name)
{
  int fd = open (cache_name, O_RDONLY);
  if (fd < 0)
    error (EXIT_FAILURE, errno, _("Can't open cache file %s\n"), cache_name);

  struct stat64 st;
  if (fstat64 (fd, &st) < 0
      /* No need to map the file if it is empty.  */
      || st.st_size == 0)
    {
      close (fd);
      return;
    }

  struct cache_file *cache
    = mmap (NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (cache == MAP_FAILED)
    error (EXIT_FAILURE, errno, _("mmap of cache file failed.\n"));

  size_t cache_size = st.st_size;
  if (cache_size < sizeof (struct cache_file))
    error (EXIT_FAILURE, 0, _("File is not a cache file.\n"));

  struct cache_file_new *cache_new = NULL;
  const char *cache_data;
  int format = 0;

  if (memcmp (cache->magic, CACHEMAGIC, sizeof CACHEMAGIC - 1))
    {
      /* This can only be the new format without the old one.  */
      cache_new = (struct cache_file_new *) cache;

      if (memcmp (cache_new->magic, CACHEMAGIC_NEW, sizeof CACHEMAGIC_NEW - 1)
	  || memcmp (cache_new->version, CACHE_VERSION,
		      sizeof CACHE_VERSION - 1))
	error (EXIT_FAILURE, 0, _("File is not a cache file.\n"));
      format = 1;
      /* This is where the strings start.  */
      cache_data = (const char *) cache_new;
    }
  else
    {
      size_t offset = ALIGN_CACHE (sizeof (struct cache_file)
				   + (cache->nlibs
				      * sizeof (struct file_entry)));
      /* This is where the strings start.  */
      cache_data = (const char *) &cache->libs[cache->nlibs];

      /* Check for a new cache embedded in the old format.  */
      if (cache_size >
	  (offset + sizeof (struct cache_file_new)))
	{

	  cache_new = (struct cache_file_new *) ((void *)cache + offset);

	  if (memcmp (cache_new->magic, CACHEMAGIC_NEW,
		      sizeof CACHEMAGIC_NEW - 1) == 0
	      && memcmp (cache_new->version, CACHE_VERSION,
			 sizeof CACHE_VERSION - 1) == 0)
	    {
	      cache_data = (const char *) cache_new;
	      format = 1;
	    }
	}
    }

  if (format == 0)
    {
      printf (_("%d libs found in cache `%s'\n"), cache->nlibs, cache_name);

      /* Print everything.  */
      for (unsigned int i = 0; i < cache->nlibs; i++)
	print_entry (cache_data + cache->libs[i].key,
		     cache->libs[i].flags, 0, 0,
		     cache_data + cache->libs[i].value);
    }
  else if (format == 1)
    {
      printf (_("%d libs found in cache `%s'\n"),
	      cache_new->nlibs, cache_name);

      /* Print everything.  */
      for (unsigned int i = 0; i < cache_new->nlibs; i++)
	print_entry (cache_data + cache_new->libs[i].key,
		     cache_new->libs[i].flags,
		     cache_new->libs[i].osversion,
		     cache_new->libs[i].hwcap,
		     cache_data + cache_new->libs[i].value);
    }
  /* Cleanup.  */
  munmap (cache, cache_size);
  close (fd);
}

/* Initialize cache data structures.  */
void
init_cache (void)
{
  entries = NULL;
}

static int
compare (const struct cache_entry *e1, const struct cache_entry *e2)
{
  /* We need to swap entries here to get the correct sort order.  */
  int res = _dl_cache_libcmp (e2->lib, e1->lib);
  if (res == 0)
    {
      if (e1->flags < e2->flags)
	return 1;
      else if (e1->flags > e2->flags)
	return -1;
      /* Sort by most specific hwcap.  */
      else if (e2->bits_hwcap > e1->bits_hwcap)
	return 1;
      else if (e2->bits_hwcap < e1->bits_hwcap)
	return -1;
      else if (e2->hwcap > e1->hwcap)
	return 1;
      else if (e2->hwcap < e1->hwcap)
	return -1;
      if (e2->osversion > e1->osversion)
	return 1;
      if (e2->osversion < e1->osversion)
	return -1;
    }
  return res;
}

/* Save the contents of the cache.  */
void
save_cache (const char *cache_name)
{
  /* The cache entries are sorted already, save them in this order. */

  /* Count the length of all strings.  */
  /* The old format doesn't contain hwcap entries and doesn't contain
     libraries in subdirectories with hwcaps entries.  Count therefore
     also all entries with hwcap == 0.  */
  size_t total_strlen = 0;
  struct cache_entry *entry;
  /* Number of cache entries.  */
  int cache_entry_count = 0;
  /* Number of normal cache entries.  */
  int cache_entry_old_count = 0;

  for (entry = entries; entry != NULL; entry = entry->next)
    {
      /* Account the final NULs.  */
      total_strlen += strlen (entry->lib) + strlen (entry->path) + 2;
      ++cache_entry_count;
      if (entry->hwcap == 0)
	++cache_entry_old_count;
    }

  /* Create the on disk cache structure.  */
  struct cache_file *file_entries = NULL;
  size_t file_entries_size = 0;

  if (opt_format != 2)
    {
      /* struct cache_file_new is 64-bit aligned on some arches while
	 only 32-bit aligned on other arches.  Duplicate last old
	 cache entry so that new cache in ld.so.cache can be used by
	 both.  */
      if (opt_format != 0)
	cache_entry_old_count = (cache_entry_old_count + 1) & ~1;

      /* And the list of all entries in the old format.  */
      file_entries_size = sizeof (struct cache_file)
	+ cache_entry_old_count * sizeof (struct file_entry);
      file_entries = xmalloc (file_entries_size);

      /* Fill in the header.  */
      memset (file_entries, '\0', sizeof (struct cache_file));
      memcpy (file_entries->magic, CACHEMAGIC, sizeof CACHEMAGIC - 1);

      file_entries->nlibs = cache_entry_old_count;
    }

  struct cache_file_new *file_entries_new = NULL;
  size_t file_entries_new_size = 0;

  if (opt_format != 0)
    {
      /* And the list of all entries in the new format.  */
      file_entries_new_size = sizeof (struct cache_file_new)
	+ cache_entry_count * sizeof (struct file_entry_new);
      file_entries_new = xmalloc (file_entries_new_size);

      /* Fill in the header.  */
      memset (file_entries_new, '\0', sizeof (struct cache_file_new));
      memcpy (file_entries_new->magic, CACHEMAGIC_NEW,
	      sizeof CACHEMAGIC_NEW - 1);
      memcpy (file_entries_new->version, CACHE_VERSION,
	      sizeof CACHE_VERSION - 1);

      file_entries_new->nlibs = cache_entry_count;
      file_entries_new->len_strings = total_strlen;
    }

  /* Pad for alignment of cache_file_new.  */
  size_t pad = ALIGN_CACHE (file_entries_size) - file_entries_size;

  /* If we have both formats, we hide the new format in the strings
     table, we have to adjust all string indices for this so that
     old libc5/glibc 2 dynamic linkers just ignore them.  */
  unsigned int str_offset;
  if (opt_format != 0)
    str_offset = file_entries_new_size;
  else
    str_offset = 0;

  /* An array for all strings.  */
  char *strings = xmalloc (total_strlen);
  char *str = strings;
  int idx_old;
  int idx_new;

  for (idx_old = 0, idx_new = 0, entry = entries; entry != NULL;
       entry = entry->next, ++idx_new)
    {
      /* First the library.  */
      if (opt_format != 2 && entry->hwcap == 0)
	{
	  file_entries->libs[idx_old].flags = entry->flags;
	  /* XXX: Actually we can optimize here and remove duplicates.  */
	  file_entries->libs[idx_old].key = str_offset + pad;
	}
      if (opt_format != 0)
	{
	  /* We could subtract file_entries_new_size from str_offset -
	     not doing so makes the code easier, the string table
	     always begins at the beginning of the the new cache
	     struct.  */
	  file_entries_new->libs[idx_new].flags = entry->flags;
	  file_entries_new->libs[idx_new].osversion = entry->osversion;
	  file_entries_new->libs[idx_new].hwcap = entry->hwcap;
	  file_entries_new->libs[idx_new].key = str_offset;
	}

      size_t len = strlen (entry->lib) + 1;
      str = mempcpy (str, entry->lib, len);
      str_offset += len;
      /* Then the path.  */
      if (opt_format != 2 && entry->hwcap == 0)
	file_entries->libs[idx_old].value = str_offset + pad;
      if (opt_format != 0)
	file_entries_new->libs[idx_new].value = str_offset;
      len = strlen (entry->path) + 1;
      str = mempcpy (str, entry->path, len);
      str_offset += len;
      /* Ignore entries with hwcap for old format.  */
      if (entry->hwcap == 0)
	++idx_old;
    }

  /* Duplicate last old cache entry if needed.  */
  if (opt_format != 2
      && idx_old < cache_entry_old_count)
    file_entries->libs[idx_old] = file_entries->libs[idx_old - 1];

  /* Write out the cache.  */

  /* Write cache first to a temporary file and rename it later.  */
  char *temp_name = xmalloc (strlen (cache_name) + 2);
  sprintf (temp_name, "%s~", cache_name);

  /* Create file.  */
  int fd = open (temp_name, O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW,
		 S_IRUSR|S_IWUSR);
  if (fd < 0)
    error (EXIT_FAILURE, errno, _("Can't create temporary cache file %s"),
	   temp_name);

  /* Write contents.  */
  if (opt_format != 2)
    {
      if (write (fd, file_entries, file_entries_size)
	  != (ssize_t) file_entries_size)
	error (EXIT_FAILURE, errno, _("Writing of cache data failed"));
    }
  if (opt_format != 0)
    {
      /* Align cache.  */
      if (opt_format != 2)
	{
	  char zero[pad];
	  memset (zero, '\0', pad);
	  if (write (fd, zero, pad) != (ssize_t) pad)
	    error (EXIT_FAILURE, errno, _("Writing of cache data failed"));
	}
      if (write (fd, file_entries_new, file_entries_new_size)
	  != (ssize_t) file_entries_new_size)
	error (EXIT_FAILURE, errno, _("Writing of cache data failed"));
    }

  if (write (fd, strings, total_strlen) != (ssize_t) total_strlen
      || close (fd))
    error (EXIT_FAILURE, errno, _("Writing of cache data failed"));

  /* Make sure user can always read cache file */
  if (chmod (temp_name, S_IROTH|S_IRGRP|S_IRUSR|S_IWUSR))
    error (EXIT_FAILURE, errno,
	   _("Changing access rights of %s to %#o failed"), temp_name,
	   S_IROTH|S_IRGRP|S_IRUSR|S_IWUSR);

  /* Move temporary to its final location.  */
  if (rename (temp_name, cache_name))
    error (EXIT_FAILURE, errno, _("Renaming of %s to %s failed"), temp_name,
	   cache_name);

  /* Free all allocated memory.  */
  free (file_entries_new);
  free (file_entries);
  free (strings);

  while (entries)
    {
      entry = entries;
      entries = entries->next;
      free (entry);
    }
}


/* Add one library to the cache.  */
void
add_to_cache (const char *path, const char *lib, int flags,
	      unsigned int osversion, uint64_t hwcap)
{
  size_t liblen = strlen (lib) + 1;
  size_t len = liblen + strlen (path) + 1;
  struct cache_entry *new_entry
    = xmalloc (sizeof (struct cache_entry) + liblen + len);

  new_entry->lib = memcpy ((char *) (new_entry + 1), lib, liblen);
  new_entry->path = new_entry->lib + liblen;
  snprintf (new_entry->path, len, "%s/%s", path, lib);
  new_entry->flags = flags;
  new_entry->osversion = osversion;
  new_entry->hwcap = hwcap;
  new_entry->bits_hwcap = 0;

  /* Count the number of bits set in the masked value.  */
  for (size_t i = 0;
       (~((1ULL << i) - 1) & hwcap) != 0 && i < 8 * sizeof (hwcap); ++i)
    if ((hwcap & (1ULL << i)) != 0)
      ++new_entry->bits_hwcap;


  /* Keep the list sorted - search for right place to insert.  */
  struct cache_entry *ptr = entries;
  struct cache_entry *prev = entries;
  while (ptr != NULL)
    {
      if (compare (ptr, new_entry) > 0)
	break;
      prev = ptr;
      ptr = ptr->next;
    }
  /* Is this the first entry?  */
  if (ptr == entries)
    {
      new_entry->next = entries;
      entries = new_entry;
    }
  else
    {
      new_entry->next = prev->next;
      prev->next = new_entry;
    }
}


/* Auxiliary cache.  */

struct aux_cache_entry_id
{
  uint64_t ino;
  uint64_t ctime;
  uint64_t size;
  uint64_t dev;
};

struct aux_cache_entry
{
  struct aux_cache_entry_id id;
  int flags;
  unsigned int osversion;
  int used;
  char *soname;
  struct aux_cache_entry *next;
};

#define AUX_CACHEMAGIC		"glibc-ld.so.auxcache-1.0"

struct aux_cache_file_entry
{
  struct aux_cache_entry_id id;	/* Unique id of entry.  */
  int32_t flags;		/* This is 1 for an ELF library.  */
  uint32_t soname;		/* String table indice.  */
  uint32_t osversion;		/* Required OS version.	 */
  int32_t pad;
};

/* ldconfig maintains an auxiliary cache file that allows
   only reading those libraries that have changed since the last iteration.
   For this for each library some information is cached in the auxiliary
   cache.  */
struct aux_cache_file
{
  char magic[sizeof AUX_CACHEMAGIC - 1];
  uint32_t nlibs;		/* Number of entries.  */
  uint32_t len_strings;		/* Size of string table. */
  struct aux_cache_file_entry libs[0]; /* Entries describing libraries.  */
  /* After this the string table of size len_strings is found.	*/
};

static const unsigned int primes[] =
{
  1021, 2039, 4093, 8191, 16381, 32749, 65521, 131071, 262139,
  524287, 1048573, 2097143, 4194301, 8388593, 16777213, 33554393,
  67108859, 134217689, 268435399, 536870909, 1073741789, 2147483647
};

static size_t aux_hash_size;
static struct aux_cache_entry **aux_hash;

/* Simplistic hash function for aux_cache_entry_id.  */
static unsigned int
aux_cache_entry_id_hash (struct aux_cache_entry_id *id)
{
  uint64_t ret = ((id->ino * 11 + id->ctime) * 11 + id->size) * 11 + id->dev;
  return ret ^ (ret >> 32);
}

static size_t nextprime (size_t x)
{
  for (unsigned int i = 0; i < sizeof (primes) / sizeof (primes[0]); ++i)
    if (primes[i] >= x)
      return primes[i];
  return x;
}

void
init_aux_cache (void)
{
  aux_hash_size = primes[3];
  aux_hash = xcalloc (aux_hash_size, sizeof (struct aux_cache_entry *));
}

int
search_aux_cache (struct stat64 *stat_buf, int *flags,
		  unsigned int *osversion, char **soname)
{
  struct aux_cache_entry_id id;
  id.ino = (uint64_t) stat_buf->st_ino;
  id.ctime = (uint64_t) stat_buf->st_ctime;
  id.size = (uint64_t) stat_buf->st_size;
  id.dev = (uint64_t) stat_buf->st_dev;

  unsigned int hash = aux_cache_entry_id_hash (&id);
  struct aux_cache_entry *entry;
  for (entry = aux_hash[hash % aux_hash_size]; entry; entry = entry->next)
    if (id.ino == entry->id.ino
	&& id.ctime == entry->id.ctime
	&& id.size == entry->id.size
	&& id.dev == entry->id.dev)
      {
	*flags = entry->flags;
	*osversion = entry->osversion;
	if (entry->soname != NULL)
	  *soname = xstrdup (entry->soname);
	else
	  *soname = NULL;
	entry->used = 1;
	return 1;
      }

  return 0;
}

static void
insert_to_aux_cache (struct aux_cache_entry_id *id, int flags,
		     unsigned int osversion, const char *soname, int used)
{
  size_t hash = aux_cache_entry_id_hash (id) % aux_hash_size;
  struct aux_cache_entry *entry;
  for (entry = aux_hash[hash]; entry; entry = entry->next)
    if (id->ino == entry->id.ino
	&& id->ctime == entry->id.ctime
	&& id->size == entry->id.size
	&& id->dev == entry->id.dev)
      abort ();

  size_t len = soname ? strlen (soname) + 1 : 0;
  entry = xmalloc (sizeof (struct aux_cache_entry) + len);
  entry->id = *id;
  entry->flags = flags;
  entry->osversion = osversion;
  entry->used = used;
  if (soname != NULL)
    entry->soname = memcpy ((char *) (entry + 1), soname, len);
  else
    entry->soname = NULL;
  entry->next = aux_hash[hash];
  aux_hash[hash] = entry;
}

void
add_to_aux_cache (struct stat64 *stat_buf, int flags,
		  unsigned int osversion, const char *soname)
{
  struct aux_cache_entry_id id;
  id.ino = (uint64_t) stat_buf->st_ino;
  id.ctime = (uint64_t) stat_buf->st_ctime;
  id.size = (uint64_t) stat_buf->st_size;
  id.dev = (uint64_t) stat_buf->st_dev;
  insert_to_aux_cache (&id, flags, osversion, soname, 1);
}

/* Load auxiliary cache to search for unchanged entries.   */
void
load_aux_cache (const char *aux_cache_name)
{
  int fd = open (aux_cache_name, O_RDONLY);
  if (fd < 0)
    {
      init_aux_cache ();
      return;
    }

  struct stat64 st;
  if (fstat64 (fd, &st) < 0 || st.st_size < sizeof (struct aux_cache_file))
    {
      close (fd);
      init_aux_cache ();
      return;
    }

  size_t aux_cache_size = st.st_size;
  struct aux_cache_file *aux_cache
    = mmap (NULL, aux_cache_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (aux_cache == MAP_FAILED
      || aux_cache_size < sizeof (struct aux_cache_file)
      || memcmp (aux_cache->magic, AUX_CACHEMAGIC, sizeof AUX_CACHEMAGIC - 1)
      || aux_cache->nlibs < 0
      || aux_cache->nlibs >= aux_cache_size)
    {
      close (fd);
      init_aux_cache ();
      return;
    }

  aux_hash_size = nextprime (aux_cache->nlibs);
  aux_hash = xcalloc (aux_hash_size, sizeof (struct aux_cache_entry *));

  const char *aux_cache_data
    = (const char *) &aux_cache->libs[aux_cache->nlibs];
  for (unsigned int i = 0; i < aux_cache->nlibs; ++i)
    insert_to_aux_cache (&aux_cache->libs[i].id,
			 aux_cache->libs[i].flags,
			 aux_cache->libs[i].osversion,
			 aux_cache->libs[i].soname == 0
			 ? NULL : aux_cache_data + aux_cache->libs[i].soname,
			 0);

  munmap (aux_cache, aux_cache_size);
  close (fd);
}

/* Save the contents of the auxiliary cache.  */
void
save_aux_cache (const char *aux_cache_name)
{
  /* Count the length of all sonames.  We start with empty string.  */
  size_t total_strlen = 1;
  /* Number of cache entries.  */
  int cache_entry_count = 0;

  for (size_t i = 0; i < aux_hash_size; ++i)
    for (struct aux_cache_entry *entry = aux_hash[i];
	 entry != NULL; entry = entry->next)
      if (entry->used)
	{
	  ++cache_entry_count;
	  if (entry->soname != NULL)
	    total_strlen += strlen (entry->soname) + 1;
	}

  /* Auxiliary cache.  */
  size_t file_entries_size
    = sizeof (struct aux_cache_file)
      + cache_entry_count * sizeof (struct aux_cache_file_entry);
  struct aux_cache_file *file_entries
    = xmalloc (file_entries_size + total_strlen);

  /* Fill in the header of the auxiliary cache.  */
  memset (file_entries, '\0', sizeof (struct aux_cache_file));
  memcpy (file_entries->magic, AUX_CACHEMAGIC, sizeof AUX_CACHEMAGIC - 1);

  file_entries->nlibs = cache_entry_count;
  file_entries->len_strings = total_strlen;

  /* Initial String offset for auxiliary cache is always after the
     special empty string.  */
  unsigned int str_offset = 1;

  /* An array for all strings.  */
  char *str = (char *) file_entries + file_entries_size;
  *str++ = '\0';

  size_t idx = 0;
  for (size_t i = 0; i < aux_hash_size; ++i)
    for (struct aux_cache_entry *entry = aux_hash[i];
	 entry != NULL; entry = entry->next)
      if (entry->used)
	{
	  file_entries->libs[idx].id = entry->id;
	  file_entries->libs[idx].flags = entry->flags;
	  if (entry->soname == NULL)
	    file_entries->libs[idx].soname = 0;
	  else
	    {
	      file_entries->libs[idx].soname = str_offset;

	      size_t len = strlen (entry->soname) + 1;
	      str = mempcpy (str, entry->soname, len);
	      str_offset += len;
	    }
	  file_entries->libs[idx].osversion = entry->osversion;
	  file_entries->libs[idx++].pad = 0;
	}

  /* Write out auxiliary cache file.  */
  /* Write auxiliary cache first to a temporary file and rename it later.  */

  char *temp_name = xmalloc (strlen (aux_cache_name) + 2);
  sprintf (temp_name, "%s~", aux_cache_name);

  /* Check that directory exists and create if needed.  */
  char *dir = strdupa (aux_cache_name);
  dir = dirname (dir);

  struct stat64 st;
  if (stat64 (dir, &st) < 0)
    {
      if (mkdir (dir, 0700) < 0)
	goto out_fail;
    }

  /* Create file.  */
  int fd = open (temp_name, O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW,
		 S_IRUSR|S_IWUSR);
  if (fd < 0)
    goto out_fail;

  if (write (fd, file_entries, file_entries_size + total_strlen)
      != (ssize_t) (file_entries_size + total_strlen)
      || close (fd))
    {
      unlink (temp_name);
      goto out_fail;
    }

  /* Move temporary to its final location.  */
  if (rename (temp_name, aux_cache_name))
    unlink (temp_name);

out_fail:
  /* Free allocated memory.  */
  free (file_entries);
}
