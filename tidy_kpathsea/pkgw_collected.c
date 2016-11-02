/* Collected kpathsea files in the tidied workalike version.

   Copyright 1993, 1994, 1995, 2008, 2009, 2010, 2011 Karl Berry.
   Copyright 1997, 2002, 2005 Olaf Weber.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this library; if not, see <http://www.gnu.org/licenses/>.  */

#include <tidy_kpathsea/config.h>
#include <tidy_kpathsea/pkgw_collected.h>
#include <tidy_kpathsea/c-pathch.h>
#include <tidy_kpathsea/c-stat.h>
#include <tidy_kpathsea/cnf.h>
#include <tidy_kpathsea/concatn.h>
#include <tidy_kpathsea/db.h>
#include <tidy_kpathsea/debug.h>
#include <tidy_kpathsea/fn.h>
#include <tidy_kpathsea/fontmap.h>
#include <tidy_kpathsea/hash.h>
#include <tidy_kpathsea/line.h>
#include <tidy_kpathsea/paths.h>
#include <tidy_kpathsea/pathsearch.h>
#include <tidy_kpathsea/str-list.h>
#include <tidy_kpathsea/tex-file.h>
#include <tidy_kpathsea/variable.h>
#include <tidy_kpathsea/xstat.h>

/* absolute.c  */

boolean
kpathsea_absolute_p (kpathsea kpse, const_string filename, boolean relative_ok)
{
  boolean absolute;
  boolean explicit_relative;

  absolute = IS_DIR_SEP (*filename);
  explicit_relative
    = relative_ok
      && (*filename == '.' && (IS_DIR_SEP (filename[1])
                         || (filename[1] == '.' && IS_DIR_SEP (filename[2]))));

  (void)kpse; /* currenty not used */
  /* FIXME: On UNIX an IS_DIR_SEP of any but the last character in the name
     implies relative.  */
  return absolute || explicit_relative;
}

boolean
kpse_absolute_p (const_string filename, boolean relative_ok)
{
    return kpathsea_absolute_p (kpse_def, filename, relative_ok);
}

/* atou.c */

unsigned
atou (const_string s)
{
  int i = atoi (s);

  if (i < 0)
    FATAL1 ("I expected a positive number, not %d", i);
  return i;
}

/* cnf.c */


/* By using our own hash table, instead of the environment, we
   complicate variable expansion (because we have to look in two
   places), but we don't bang so much on the system.  DOS and System V
   have very limited environment space.  Also, this way
   `kpse_init_format' can distinguish between values originating from
   the cnf file and ones from environment variables, which can be useful
   for users trying to figure out what's going on.  */

#define CNF_HASH_SIZE 751
#define CNF_NAME "texmf.cnf"


/* Do a single line in a cnf file: if it's blank or a comment or
   erroneous, skip it.  Otherwise, parse
     <variable>[.<program>] [=] <value>
   Do this even if the <variable> is already set in the environment,
   since the envvalue might contain a trailing :, in which case we'll be
   looking for the cnf value.

   We return NULL if ok, an error string otherwise.  */

static string
do_line (kpathsea kpse, string line)
{
  unsigned len;
  string start;
  string value, var;
  string prog = NULL;

  /* Skip leading whitespace.  */
  while (*line && ISSPACE (*line))
    line++;

  /* More to do only if we have non-comment material left.  */
  if (*line == 0 || *line == '%' || *line == '#')
    return NULL;

  /* Remove trailing comment: a % or # preceded by whitespace.  Also
     remove any whitespace before that.  For example, the value for
       foo = a#b  %something
     is a#b.  */
  value = line + strlen (line) - 1; /* start at end of line */
  while (value > line) {
    if (*value == '%' || *value == '#') {
      value--;                      /* move before comment char */
      while (ISSPACE (*value))
        *value-- = 0;               /* wipe out as much preceding whitespace
      continue;                        (and comment) as we find */
    }
    value--;                        /* move before the new null byte */
  }

  /* The variable name is everything up to the next space or = or `.'.  */
  start = line;
  while (*line && !ISSPACE (*line) && *line != '=' && *line != '.')
    line++;

  /* `line' is now one character past the end of the variable name.  */
  len = line - start;
  if (len == 0) {
    return ("No cnf variable name");
  }

  var = (string) xmalloc (len + 1);
  strncpy (var, start, len);
  var[len] = 0;

  /* If the variable is qualified with a program name, find out which. */
  while (*line && ISSPACE (*line))
    line++;
  if (*line == '.') {
    /* Skip spaces, then everything up to the next space or =.  */
    line++;
    while (ISSPACE (*line))
      line++;
    start = line;
    while (!ISSPACE (*line) && *line != '=')
      line++;

    /* It's annoying to repeat all this, but making a tokenizing
       subroutine would be just as long and annoying.  */
    len = line - start;
    prog = (string) xmalloc (len + 1);
    strncpy (prog, start, len);
    prog[len] = 0;
  }

  /* Skip whitespace, an optional =, more whitespace.  */
  while (*line && ISSPACE (*line))
    line++;
  if (*line == '=') {
    line++;
    while (*line && ISSPACE (*line))
      line++;
  }

  /* The value is whatever remains.  Remove trailing whitespace.  */
  start = line;
  len = strlen (start);
  while (len > 0 && ISSPACE (start[len - 1]))
    len--;
  if (len == 0) {
    return ("No cnf value");
  }

  value = (string) xmalloc (len + 1);
  strncpy (value, start, len);
  value[len] = 0;

  /* Suppose we want to write a single texmf.cnf that can be used under
     both NT and Unix.  This is feasible except for the path separators
     : on Unix, ; on NT.  We can't switch NT to allowing :'s, since :
     is the drive separator.  So we switch Unix to allowing ;'s.  On the
     other hand, we don't want to change IS_ENV_SEP and all the rest.

     So, simply translate all ;'s in the path
     values to :'s if we are a Unix binary.  (Fortunately we don't use ;
     in other kinds of texmf.cnf values.)  */

  if (IS_ENV_SEP(':')) {
      string loc;
      for (loc = value; *loc; loc++) {
          if (*loc == ';')
              *loc = ':';
      }
  }

  /* We want TEXINPUTS.prog to override plain TEXINPUTS.  The simplest
     way is to put both in the hash table (so we don't have to write
     hash_delete and hash_replace, and keep track of values' sources),
     and then look up the .prog version first in `kpse_cnf_get'.  */
  if (prog) {
    string lhs = concat3 (var, ".", prog);
    free (var);
    free (prog);
    var = lhs;
  }
  /* last-ditch debug */
  /* fprintf (stderr, "kpse/cnf.c hash_insert(%s,%s)\n", var, value); */
  hash_insert (&(kpse->cnf_hash), var, value);

  /* We should check that anything remaining is preceded by a comment
     character, but we don't.  Sorry.  */
  return NULL;
}

/* Read all the configuration files in the path.  */

static void
read_all_cnf (kpathsea kpse)
{
  kpse->cnf_hash = hash_create (CNF_HASH_SIZE);
}

/* Read the cnf files on the first call.  Return the first value in the
   returned list -- this will be from the last-read cnf file.  */

const_string
kpathsea_cnf_get (kpathsea kpse, const_string name)
{
  string ctry;
  const_string ret, *ret_list;

  /* When we expand the compile-time value for DEFAULT_TEXMFCNF,
     we end up needing the value for TETEXDIR and other variables,
     so kpse_var_expand ends up calling us again.  No good.  Except this
     code is not sufficient, somehow the ls-R path needs to be
     computed when initializing the cnf path.  Better to ensure that the
     compile-time path does not contain variable references.  */
  if (kpse->doing_cnf_init)
    return NULL;

  if (kpse->cnf_hash.size == 0) {
    /* Read configuration files and initialize databases.  */
    kpse->doing_cnf_init = true;
    read_all_cnf (kpse);
    kpse->doing_cnf_init = false;

    /* Since `kpse_init_db' recursively calls us, we must call it from
       outside a `kpse_path_element' loop (namely, the one in
       `read_all_cnf' above): `kpse_path_element' is not reentrant.  */
    kpathsea_init_db (kpse);
  }

  /* First look up NAME.`kpse->program_name', then NAME.  */
  assert (kpse->program_name);
  ctry = concat3 (name, ".", kpse->program_name);
  ret_list = hash_lookup (kpse->cnf_hash, ctry);
  free (ctry);
  if (ret_list) {
    ret = *ret_list;
    free (ret_list);
  } else {
    ret_list = hash_lookup (kpse->cnf_hash, name);
    if (ret_list) {
      ret = *ret_list;
      free (ret_list);
    } else {
      ret = NULL;
    }
  }

  return ret;
}

#if defined(KPSE_COMPAT_API)
const_string
kpse_cnf_get (const_string name)
{
    return kpathsea_cnf_get(kpse_def, name);
}
#endif

/* concat3.c */

string
concat3 (const_string s1,  const_string s2,  const_string s3)
{
  int s2l = s2 ? strlen (s2) : 0;
  int s3l = s3 ? strlen (s3) : 0;
  string answer
      = (string) xmalloc (strlen(s1) + s2l + s3l + 1);
  strcpy (answer, s1);
  if (s2) strcat (answer, s2);
  if (s3) strcat (answer, s3);

  return answer;
}

/* concat.c */

/* Return the concatenation of S1 and S2.  See `concatn.c' for a
   `concatn', which takes a variable number of arguments.  */

string
concat (const_string s1,  const_string s2)
{
  unsigned s1len = strlen(s1);
  unsigned s2len = strlen(s2);
  string answer = (string) xmalloc (s1len + s2len + 1);
  strcpy (answer, s1);
  strcat (answer + s1len, s2);

  return answer;
}

/* concatn.c */

/* OK, it would be epsilon more efficient to compute the total length
   and then do the copying ourselves, but I doubt it matters in reality.  */

string
concatn (const_string str1, ...)
{
  string arg;
  string ret;
  va_list ap;

  if (!str1)
    return NULL;

  ret = xstrdup (str1);

  va_start (ap, str1);
  while ((arg = va_arg (ap, string)) != NULL)
    {
      string temp = concat (ret, arg);
      free (ret);
      ret = temp;
    }
  va_end (ap);

  return ret;
}

/* debug.c */

#ifdef KPSE_DEBUG /* whole file */

/* If the real definitions of fopen or fclose are macros, we lose -- the
   #undef won't restore them. */

FILE *
fopen (const char *filename,  const char *mode)
{
#undef fopen
  FILE *ret = fopen (filename, mode);
#if defined (KPSE_COMPAT_API)
  kpathsea kpse = kpse_def;
  if (KPATHSEA_DEBUG_P (KPSE_DEBUG_FOPEN))
    DEBUGF3 ("fopen(%s, %s) => 0x%lx\n", filename, mode, (unsigned long) ret);
#endif
  return ret;
}

int
fclose (FILE * f)
{
#undef fclose
  int ret = fclose (f);
#if defined (KPSE_COMPAT_API)
  kpathsea kpse = kpse_def;
  if (KPATHSEA_DEBUG_P (KPSE_DEBUG_FOPEN))
    DEBUGF2 ("fclose(0x%lx) => %d\n", (unsigned long) f, ret);
#endif
  return ret;
}

#endif /* KPSE DEBUG */

/* dir.c */

/* Return true if FN is a directory or a symlink to a directory,
   false if not. */

boolean
kpathsea_dir_p (kpathsea kpse, string fn)
{
  /* FIXME : using the stat() replacement in gnuw32,
         we could avoid this win32 specific code. However,
         I wonder if it would be as fast as this one is ?
  */
  struct stat stats;
  return stat (fn, &stats) == 0 && S_ISDIR (stats.st_mode);
}

#if defined(KPSE_COMPAT_API)
boolean
dir_p (string fn)
{
    return kpathsea_dir_p (kpse_def, fn);
}
#endif


/*
  Return -1 if FN isn't a directory, else its number of links.
  Duplicate the call to stat; no need to incur overhead of a function
  call for that little bit of cleanliness.

  The process is a bit different under Win32 : the first call
  memoizes the nlinks value, the following ones retrieve it.
*/
int
kpathsea_dir_links (kpathsea kpse, const_string fn, long nlinks)
{
  const_string *hash_ret;

  if (kpse->link_table.size == 0)
    kpse->link_table = hash_create (457);

#ifdef KPSE_DEBUG
  /* This is annoying, but since we're storing integers as pointers, we
     can't print them as strings.  */
  if (KPATHSEA_DEBUG_P (KPSE_DEBUG_HASH))
    kpse->debug_hash_lookup_int = true;
#endif

  hash_ret = hash_lookup (kpse->link_table, fn);

#ifdef KPSE_DEBUG
  if (KPATHSEA_DEBUG_P (KPSE_DEBUG_HASH))
    kpse->debug_hash_lookup_int = false;
#endif

  /* Have to cast the int we need to/from the const_string that the hash
     table stores for values. Let's hope an int fits in a pointer.  */
  if (hash_ret) {
      nlinks = (long) *hash_ret;
  } else {
      struct stat stats;
      if (stat (fn, &stats) == 0 && S_ISDIR (stats.st_mode))
        nlinks = stats.st_nlink;
      else
        nlinks = -1;
      /* It's up to us to copy the value.  */
      hash_insert(&(kpse->link_table), xstrdup(fn), (const_string)nlinks);

#ifdef KPSE_DEBUG
      if (KPATHSEA_DEBUG_P (KPSE_DEBUG_STAT))
        DEBUGF2 ("dir_links(%s) => %ld\n", fn, nlinks);
#endif
  }

  /* In any case, return nlinks
     (either 0, the value inserted or the value retrieved. */
  return nlinks;
}

#if defined (KPSE_COMPAT_API)
int
dir_links (const_string fn, long nlinks)
{
    return kpathsea_dir_links (kpse_def, fn, nlinks);
}
#endif

/* extend-fname.c */

const_string
extend_filename (const_string name, const_string default_suffix)
{
  const_string new_s;
  const_string suffix = find_suffix (name);

  new_s = suffix == NULL ? concat3 (name, ".", default_suffix)
                         : name;
  return new_s;
}

/* file-p.c */

/* Test whether FILENAME1 and FILENAME2 are actually the same file.  If
   stat fails on either of the names, we return false, without error.  */

boolean
same_file_p (const_string filename1,  const_string filename2)
{
    struct stat sb1, sb2;
    /* These are put in variables only so the results can be inspected
       under gdb.  */
    int r1 = stat (filename1, &sb1);
    int r2 = stat (filename2, &sb2);

    return r1 == 0 && r2 == 0 ? SAME_FILE_P (sb1, sb2) : false;
}

/* find-suffix.c */

const_string
find_suffix (const_string name)
{
  const_string dot_pos = strrchr (name, '.');
  const_string p;

  if (dot_pos == NULL)
    return NULL;

  for (p = dot_pos + 1; *p; p++) {
    if (IS_DIR_SEP (*p))
      return NULL;
  }

  return dot_pos + 1;
}

/* fn.c */



/* /usr/local/lib/texmf/fonts/public/cm/pk/ljfour/cmr10.300pk is 58
   chars, so ASCII `K' seems a good choice. */
#define CHUNK_SIZE 75


fn_type
fn_init (void)
{
  fn_type ret;

  FN_ALLOCATED (ret) = FN_LENGTH (ret) = 0;
  FN_STRING (ret) = NULL;

  return ret;
}


fn_type
fn_copy0 (const_string s,  unsigned len)
{
  fn_type ret;

  FN_ALLOCATED (ret) = CHUNK_SIZE > len ? CHUNK_SIZE : len + 1;
  FN_STRING (ret) = (string)xmalloc (FN_ALLOCATED (ret));

  strncpy (FN_STRING (ret), s, len);
  FN_STRING (ret)[len] = 0;
  FN_LENGTH (ret) = len + 1;

  return ret;
}

/* Don't think we ever try to free something that might usefully be
   empty, so give fatal error if nothing allocated.  */

void
fn_free (fn_type *f)
{
  assert (FN_STRING (*f) != NULL);
  free (FN_STRING (*f));
  FN_STRING (*f) = NULL;
  FN_ALLOCATED (*f) = 0;
  FN_LENGTH (*f) = 0;
}

/* An arithmetic increase seems more reasonable than geometric.  We
   don't increase the length member since it may be more convenient for
   the caller to add than subtract when appending the stuff that will
   presumably follow.  */

static void
grow (fn_type *f,  unsigned len)
{
  while (FN_LENGTH (*f) + len > FN_ALLOCATED (*f))
    {
      FN_ALLOCATED (*f) += CHUNK_SIZE;
      XRETALLOC (FN_STRING (*f), FN_ALLOCATED (*f), char);
    }
}


void
fn_1grow (fn_type *f,  char c)
{
  grow (f, 1);
  FN_STRING (*f)[FN_LENGTH (*f)] = c;
  FN_LENGTH (*f)++;
}


void
fn_grow (fn_type *f,  const_string source,  unsigned len)
{
  grow (f, len);
  strncpy (FN_STRING (*f) + FN_LENGTH (*f), source, len);
  FN_LENGTH (*f) += len;
}


void
fn_str_grow (fn_type *f,  const_string s)
{
  unsigned more_len = strlen (s);
  grow (f, more_len);
  strcat (FN_STRING (*f), s);
  FN_LENGTH (*f) += more_len;
}


void
fn_shrink_to (fn_type *f,  unsigned loc)
{
  assert (FN_LENGTH (*f) > loc);
  FN_STRING (*f)[loc] = 0;
  FN_LENGTH (*f) = loc + 1;
}

/* fontmap.c */

/* We have one and only one fontmap, so may as well make it static
   instead of passing it around.  */

#ifndef MAP_NAME
#define MAP_NAME "texfonts.map"
#endif
#ifndef MAP_HASH_SIZE
#define MAP_HASH_SIZE 4001
#endif


/* Return next whitespace-delimited token in STR or NULL if none.  */

static string
token (const_string str)
{
  unsigned len;
  const_string start;
  string ret;

  while (*str && ISSPACE (*str))
    str++;

  start = str;
  while (*str && !ISSPACE (*str))
    str++;

  len = str - start;
  ret = (string)xmalloc (len + 1);
  strncpy (ret, start, len);
  ret[len] = 0;

  return ret;
}

/* Open and read the mapping file MAP_FILENAME, putting its entries into
   MAP. Comments begin with % and continue to the end of the line.  Each
   line of the file defines an entry: the first word is the real
   filename (e.g., `ptmr'), the second word is the alias (e.g.,
   `Times-Roman'), and any subsequent words are ignored.  .tfm is added
   if either the filename or the alias have no extension.  This is the
   same order as in Dvips' psfonts.map.  Perhaps someday the programs
   will both read the same file.  */

static void
map_file_parse (kpathsea kpse, const_string map_filename)
{
  char *orig_l;
  unsigned map_lineno = 0;
  FILE *f = xfopen (map_filename, FOPEN_R_MODE);

  if (kpse->record_input)
    kpse->record_input (map_filename);

  while ((orig_l = read_line (f)) != NULL) {
    string filename;
    string l = orig_l; /* save for free() */
    string comment_loc = strrchr (l, '%');
    if (!comment_loc) {
      comment_loc = strstr (l, "@c");
    }

    /* Ignore anything after a % or @c.  */
    if (comment_loc)
      *comment_loc = 0;

    map_lineno++;

    /* Skip leading whitespace so we can use strlen below.  Can't use
       strtok since this routine is recursive.  */
    while (*l && ISSPACE (*l))
      l++;

    /* If we don't have any filename, that's ok, the line is blank.  */
    filename = token (l);
    if (filename) {
      string alias = token (l + strlen (filename));

      if (STREQ (filename, "include")) {
        if (alias == NULL) {
  WARNING2 ("kpathsea: %s:%u: Filename argument for include directive missing",
                    map_filename, map_lineno);
        } else {
          string include_fname = kpathsea_path_search (kpse,
                                   kpse->map_path, alias, false);
          if (include_fname) {
            map_file_parse (kpse, include_fname);
            if (include_fname != alias)
              free (include_fname);
          } else {
            WARNING3 ("kpathsea: %s:%u: Can't find fontname include file `%s'",
                      map_filename, map_lineno, alias);
          }
          free (alias);
          free (filename);
        }

      /* But if we have a filename and no alias, something's wrong.  */
      } else if (alias == NULL) {
        WARNING3 ("kpathsea: %s:%u: Fontname alias missing for filename `%s'",
                  map_filename, map_lineno, filename);
        free (filename);

      } else {
        /* We've got everything.  Insert the new entry.  They were
           already dynamically allocated by token(), so don't bother
           with xstrdup.  */
          hash_insert_normalized (&(kpse->map), alias, filename);
      }
    }

    free (orig_l);
  }

  xfclose (f, map_filename);
}

/* Parse the file MAP_NAME in each of the directories in PATH and
   return the resulting structure.  Entries in earlier files override
   later files.  */

static void
read_all_maps (kpathsea kpse)
{
  string *filenames;

  kpse->map_path = kpathsea_init_format (kpse, kpse_fontmap_format);
  filenames = kpathsea_all_path_search (kpse, kpse->map_path, MAP_NAME);

  kpse->map = hash_create (MAP_HASH_SIZE);

  while (*filenames) {
    map_file_parse (kpse, *filenames);
    filenames++;
  }
}

/* Look up KEY in texfonts.map's; if it's not found, remove any suffix
   from KEY and try again.  Create the map if necessary.  */

const_string *
kpathsea_fontmap_lookup (kpathsea kpse, const_string key)
{
  const_string *ret;
  const_string suffix = find_suffix (key);

  if (kpse->map.size == 0) {
    read_all_maps (kpse);
  }

  ret = hash_lookup (kpse->map, key);
  if (!ret) {
    /* OK, the original KEY didn't work.  Let's check for the KEY without
       an extension -- perhaps they gave foobar.tfm, but the mapping only
       defines `foobar'.  */
    if (suffix) {
      string base_key = remove_suffix (key);
      ret = hash_lookup (kpse->map, base_key);
      free (base_key);
    }
  }

  /* Append any original suffix.  */
  if (ret && suffix) {
    const_string *elt;
    for (elt = ret; *elt; elt++) {
      *elt = extend_filename (*elt, suffix);
    }
  }

  return ret;
}

/* hash.c */

/* The hash function.  We go for simplicity here.  */

/* All our hash tables are related to filenames.  */

static unsigned
hash (hash_table_type table,  const_string key)
{
  unsigned n = 0;

  /* Our keys aren't often anagrams of each other, so no point in
     weighting the characters.  */
  while (*key != 0)
#if defined (WIN32) && defined (KPSE_COMPAT_API)
    if (IS_KANJI(key)) {
      n = (n + n + (unsigned)(*key++)) % table.size;
      n = (n + n + (unsigned)(*key++)) % table.size;
    } else
#endif
    n = (n + n + TRANSFORM (*key++)) % table.size;

  return n;
}

/* Identical has function as above, but does not normalize keys. */
static unsigned
hash_normalized (hash_table_type table,  const_string key)
{
  unsigned n = 0;

  /* Our keys aren't often anagrams of each other, so no point in
     weighting the characters.  */
  while (*key != 0)
    n = (n + n + (*key++)) % table.size;

  return n;
}

hash_table_type
hash_create (unsigned size)
{
  /* The was "static ..." since Oct3, 1997 to work around a gcc
     optimizer bug for Alpha. That particular optimization bug
     should be gone by now (Mar4, 2009).
  */
  hash_table_type ret;
  unsigned b;
  ret.buckets = XTALLOC (size, hash_element_type *);
  ret.size = size;

  /* calloc's zeroes aren't necessarily NULL, so be safe.  */
  for (b = 0; b <ret.size; b++)
    ret.buckets[b] = NULL;

  return ret;
}

/* Whether or not KEY is already in TABLE, insert it and VALUE.  Do not
   duplicate the strings, in case they're being purposefully shared.  */

void
hash_insert (hash_table_type *table,
             const_string key,
             const_string value)
{
  unsigned n = hash (*table, key);
  hash_element_type *new_elt = XTALLOC1 (hash_element_type);

  new_elt->key = key;
  new_elt->value = value;
  new_elt->next = NULL;

  /* Insert the new element at the end of the list.  */
  if (!table->buckets[n])
    /* first element in bucket is a special case.  */
    table->buckets[n] = new_elt;
  else
    {
      hash_element_type *loc = table->buckets[n];
      while (loc->next)         /* Find the last element.  */
        loc = loc->next;
      loc->next = new_elt;      /* Insert the new one after.  */
    }
}

/* Same as above, for normalized keys. */
void
hash_insert_normalized (hash_table_type *table,
                        const_string key,
                        const_string value)
{
  unsigned n = hash_normalized (*table, key);
  hash_element_type *new_elt = XTALLOC1 (hash_element_type);

  new_elt->key = key;
  new_elt->value = value;
  new_elt->next = NULL;

  /* Insert the new element at the end of the list.  */
  if (!table->buckets[n])
    /* first element in bucket is a special case.  */
    table->buckets[n] = new_elt;
  else
    {
      hash_element_type *loc = table->buckets[n];
      while (loc->next)         /* Find the last element.  */
        loc = loc->next;
      loc->next = new_elt;      /* Insert the new one after.  */
    }
}

/* Remove a (KEY, VALUE) pair.  */

void
hash_remove (hash_table_type *table,  const_string key,
             const_string value)
{
  hash_element_type *p;
  hash_element_type *q;
  unsigned n = hash (*table, key);

  /* Find pair.  */
  for (q = NULL, p = table->buckets[n]; p != NULL; q = p, p = p->next)
    if (FILESTRCASEEQ (key, p->key) && STREQ (value, p->value))
      break;
  if (p) {
    /* We found something, remove it from the chain.  */
    if (q) q->next = p->next; else table->buckets[n] = p->next;
    /* We cannot dispose of the contents.  */
    free (p);
  }
}

/* Look up KEY in TABLE, and return NULL-terminated list of all matching
   values (not copies), in insertion order.  If none, return NULL.  */

const_string *
hash_lookup (hash_table_type table,  const_string key)
{
  hash_element_type *p;
  cstr_list_type ret;
  unsigned n = hash (table, key);
  ret = cstr_list_init ();

  /* Look at everything in this bucket.  */
  for (p = table.buckets[n]; p != NULL; p = p->next)
    if (FILESTRCASEEQ (key, p->key))
      cstr_list_add (&ret, p->value);

  /* If we found anything, mark end of list with null.  */
  if (STR_LIST (ret))
    cstr_list_add (&ret, NULL);

#ifdef KPSE_DEBUG
#if defined (KPSE_COMPAT_API)
  {
  kpathsea kpse = kpse_def;
  if (KPATHSEA_DEBUG_P (KPSE_DEBUG_HASH))
    {
      DEBUGF1 ("hash_lookup(%s) =>", key);
      if (!STR_LIST (ret))
        fputs (" (nil)\n", stderr);
      else
        {
          const_string *r;
          for (r = STR_LIST (ret); *r; r++)
            {
              putc (' ', stderr);
              if (kpse->debug_hash_lookup_int)
                fprintf (stderr, "%ld", (long) *r);
              else
                fputs (*r, stderr);
            }
          putc ('\n', stderr);
        }
      fflush (stderr);
    }
  }
#endif
#endif

  return STR_LIST (ret);
}

#ifdef KPSE_DEBUG
/* We only print nonempty buckets, to decrease output volume.  */

void
hash_print (hash_table_type table,  boolean summary_only)
{
  unsigned b;
  unsigned total_elements = 0, total_buckets = 0;

  for (b = 0; b < table.size; b++) {
    hash_element_type *bucket = table.buckets[b];

    if (bucket) {
      unsigned len = 1;
      hash_element_type *tb;

      total_buckets++;
      if (!summary_only) fprintf (stderr, "%4d ", b);

      for (tb = bucket->next; tb != NULL; tb = tb->next)
        len++;
      if (!summary_only) fprintf (stderr, ":%-5d", len);
      total_elements += len;

      if (!summary_only) {
        for (tb = bucket; tb != NULL; tb = tb->next)
          fprintf (stderr, " %s=>%s", tb->key, tb->value);
        putc ('\n', stderr);
      }
    }
  }

  fprintf (stderr,
          "%u buckets, %u nonempty (%u%%); %u entries, average chain %.1f.\n",
          table.size,
          total_buckets,
          100 * total_buckets / table.size,
          total_elements,
          total_buckets ? total_elements / (double) total_buckets : 0.0);
}
#endif

/* kdefault.c */

/* Check for leading colon first, then trailing, then doubled, since
   that is fastest.  Usually it will be leading or trailing.  */

string
kpathsea_expand_default (kpathsea kpse, const_string path,
                         const_string fallback)
{
  unsigned path_length;
  string expansion;
  (void)kpse; /* currenty not used */

  /* The default path better not be null.  */
  assert (fallback);

  if (path == NULL)
    expansion = xstrdup (fallback);

  /* Solitary or leading :?  */
  else if (IS_ENV_SEP (*path))
    {
      expansion = path[1] == 0 ? xstrdup (fallback) : concat (fallback, path);
    }

  /* Sorry about the assignment in the middle of the expression, but
     conventions were made to be flouted and all that.  I don't see the
     point of calling strlen twice or complicating the logic just to
     avoid the assignment (especially now that I've pointed it out at
     such great length).  */
  else if (path[(path_length = strlen (path)) - 1] == ENV_SEP)
    expansion = concat (path, fallback);

  /* OK, not leading or trailing.  Check for doubled.  */
  else
    {
      const_string loc;

      for (loc = path; *loc; loc++)
        if (IS_ENV_SEP (loc[0]) && IS_ENV_SEP (loc[1]))
          break;
      if (*loc)
        { /* We have a doubled colon.  */
          expansion = (string)xmalloc (path_length + strlen(fallback) + 1);

          /* Copy stuff up to and including the first colon.  */
          strncpy (expansion, path, loc - path + 1);
          expansion[loc - path + 1] = 0;

          /* Copy in FALLBACK, and then the rest of PATH.  */
          strcat (expansion, fallback);
          strcat (expansion, loc + 1);
        }
      else
        { /* No doubled colon. */
          expansion = xstrdup(path);
        }
    }

  return expansion;
}

/* kpathsea.c */

kpathsea
kpathsea_new (void)
{
    kpathsea ret;
    ret = xcalloc(1, sizeof(kpathsea_instance));
    return ret;
}

/* Sadly, quite a lot of the freeing is not safe:
   it seems there are literals used all over. */
void
kpathsea_finish (kpathsea kpse)
{
    if (kpse==NULL)
        return;
#if defined (KPSE_COMPAT_API)
    if (kpse == kpse_def)
        return;
#endif
    free (kpse);
}

#if defined (KPSE_COMPAT_API)

kpathsea_instance kpse_def_inst;
kpathsea kpse_def = &kpse_def_inst;

#endif /* KPSE_COMPAT_API */

/* line.c */

/* Allocate in increments of this size.  */
#define LINE_C_BLOCK_SIZE 75

char *
read_line (FILE *f)
{
  int c;
  unsigned limit = LINE_C_BLOCK_SIZE;
  unsigned loc = 0;
  char *line = xmalloc (limit);

  flockfile (f);

  while ((c = getc_unlocked (f)) != EOF && c != '\n' && c != '\r') {
    line[loc] = c;
    loc++;

    /* By testing after the assignment, we guarantee that we'll always
       have space for the null we append below.  We know we always
       have room for the first char, since we start with LINE_C_BLOCK_SIZE.  */
    if (loc == limit) {
      limit += LINE_C_BLOCK_SIZE;
      line = xrealloc (line, limit);
    }
  }

  /* If we read anything, return it, even a partial last-line-if-file
     which is not properly terminated.  */
  if (loc == 0 && c == EOF) {
    /* At end of file.  */
    free (line);
    line = NULL;
  } else {
    /* Terminate the string.  We can't represent nulls in the file,
       but this doesn't matter.  */
    line[loc] = 0;
    /* Absorb LF of a CRLF pair. */
    if (c == '\r') {
      c = getc_unlocked (f);
      if (c != '\n') {
        ungetc (c, f);
      }
    }
  }

  funlockfile (f);

  return line;
}

/* tex-make.c, edited to never do anything */

string
kpathsea_make_tex (kpathsea kpse, kpse_file_format_type format,
                   const_string base)
{
  return NULL;
}

#if defined (KPSE_COMPAT_API)
string
kpse_make_tex (kpse_file_format_type format,  const_string base)
{
  return kpathsea_make_tex (kpse_def, format, base);
}
#endif
