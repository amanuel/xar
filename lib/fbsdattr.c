/*
 * Copyright (c) 2004 Rob Braun
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Rob Braun nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * 28-Oct-2004
 * DRI: Rob Braun <bbraun@synack.net>
 */
/*
 * Portions Copyright 2006, Apple Computer, Inc.
 * Christopher Ryan <ryanc@apple.com>
*/

#include "config.h"
#include <unistd.h>
#include "xar.h"
#include "arcmod.h"
#include "b64.h"
#include "io.h"
#include "archive.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>

/* FreeBSD Extended Attribute Headers */
#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/extattr.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
struct _fbsdattr_context{
	const char *file;
	const char *attr;
	void *buf;
	int off;
	int bufsz;
	int ns;
};

#define FBSDATTR_CONTEXT(x) ((struct _fbsdattr_context *)(x))

int32_t xar_fbsdattr_read(xar_t x, xar_file_t f, void *buf, size_t len, void *context) {
	if( !FBSDATTR_CONTEXT(context)->buf ) {
		FBSDATTR_CONTEXT(context)->bufsz = extattr_get_link(FBSDATTR_CONTEXT(context)->file, FBSDATTR_CONTEXT(context)->ns, FBSDATTR_CONTEXT(context)->attr, NULL, 0);
		if( FBSDATTR_CONTEXT(context)->bufsz < 0 )
			return -1;
		FBSDATTR_CONTEXT(context)->buf = malloc(FBSDATTR_CONTEXT(context)->bufsz);
		if( !FBSDATTR_CONTEXT(context)->buf )
			return -1;

		FBSDATTR_CONTEXT(context)->bufsz = extattr_get_link(FBSDATTR_CONTEXT(context)->file, FBSDATTR_CONTEXT(context)->ns, FBSDATTR_CONTEXT(context)->attr, FBSDATTR_CONTEXT(context)->buf, FBSDATTR_CONTEXT(context)->bufsz);
	}

	if( (FBSDATTR_CONTEXT(context)->bufsz - FBSDATTR_CONTEXT(context)->off) <= len ) {
		int32_t ret;

		ret = FBSDATTR_CONTEXT(context)->bufsz - FBSDATTR_CONTEXT(context)->off;
		memcpy(buf, FBSDATTR_CONTEXT(context)->buf+FBSDATTR_CONTEXT(context)->off, ret);
		FBSDATTR_CONTEXT(context)->off += ret;
		return(ret);
	} else {
		memcpy(buf, FBSDATTR_CONTEXT(context)->buf+FBSDATTR_CONTEXT(context)->off, len);
		FBSDATTR_CONTEXT(context)->buf += len;
		return(len);
	}

}
int32_t xar_fbsdattr_write(xar_t x, xar_file_t f, void *buf, size_t len, void *context) {
	return extattr_set_link(FBSDATTR_CONTEXT(context)->file, FBSDATTR_CONTEXT(context)->ns, FBSDATTR_CONTEXT(context)->attr, buf, len);
}
#endif

int32_t xar_fbsdattr_archive(xar_t x, xar_file_t f, const char* file, const char *buffer, size_t len)
{
#ifdef HAVE_SYS_EXTATTR_H
	char *buf = NULL;
	int ret, retval=0, bufsz, i;
#if defined(HAVE_STATVFS) && defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
	struct statvfs sfs;
#else
	struct statfs sfs;
#endif
	char *fsname = NULL;
	int namespace = EXTATTR_NAMESPACE_USER;
	struct _fbsdattr_context context;
	
	memset(&context,0,sizeof(struct _fbsdattr_context));

	/* no fbsdattr attributes for data to a buffer */
	if(len)
		return 0;
	if(file == NULL)
		return 0;
	
TRYAGAIN:
	/* extattr_list_link()'s man page does not define the return
	 * value.  The kernel source comments say 0 for success, -1 for
	 * failure.  However, the observed behavior is # of bytes 
	 * used, 0 if none, -1 on error.
	 * Also, errno is not documented to be set to anything useful
	 * if buf is too small.  We are using an undocumented "feature"
	 * that if the data argument is NULL, it will return the number
	 * of bytes that would have been written (beware, return value
	 * does not indicate success or failure on it's own.  Need to
	 * check the return value *and* the parameters.
	 */
	ret = extattr_list_link(file, namespace, NULL, 0);
	if( ret < 0 ) {
		if( namespace == EXTATTR_NAMESPACE_USER ) {
			namespace = EXTATTR_NAMESPACE_SYSTEM;
			goto TRYAGAIN;
		} else {
			/* If we get eperm on system namespace, don't
			 * return error.  This is expected for normal
			 * users trying to archive the system namespace
			 * on freebsd 6.2.  On netbsd 3.1, they've decided
			 * to return EOPNOTSUPP instead.
			 */
			if( errno == EPERM )
				ret = 0;
			else if( errno == EOPNOTSUPP )
				ret = 0;
			else {
				xar_err_new(x);
				xar_err_set_file(x, f);
				xar_err_set_string(x, "Error archiving EA"); 
				xar_err_callback(x, XAR_SEVERITY_NONFATAL, XAR_ERR_ARCHIVE_CREATION);
				ret = 0;
			}
			goto BAIL;
		}
	}
	bufsz = ret;
	buf = malloc(bufsz);
	if( !buf ) {
		retval = -1;
		goto BAIL;
	}
	memset(buf, 0, bufsz);
	ret = extattr_list_link(file, namespace, buf, bufsz);
	if( ret < 0 ) {
		switch(errno) {
		case ENOTSUP: retval=0; goto BAIL;
		default: retval=-1; goto BAIL;
		};
	}
	/* Even though 0 is a documented success, observed behavior
	 * indicates 0 means all perms were satisfied, etc, but
	 * no extattrs were found to list.
	 */
	if( ret == 0 ) {
		if( namespace == EXTATTR_NAMESPACE_USER ) {
			namespace = EXTATTR_NAMESPACE_SYSTEM;
			goto TRYAGAIN;
		} else {
			/* If we get eperm on system namespace, don't
			 * return error.  This is expected for normal
			 * users trying to archive the system namespace
			 * on freebsd 6.2.  On netbsd 3.1, they've decided
			 * to return EOPNOTSUPP instead.
			 */
			if( errno == EPERM )
				ret = 0;
			else if( errno == EOPNOTSUPP )
				ret = 0;
			else {
				xar_err_new(x);
				xar_err_set_file(x, f);
				xar_err_set_string(x, "Error archiving EA"); 
				xar_err_callback(x, XAR_SEVERITY_NONFATAL, XAR_ERR_ARCHIVE_CREATION);
				ret = 0;
			}
			goto BAIL;
		}
	}


#if defined(HAVE_STATVFS) && defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
	statvfs(file, &sfs);
#else
	statfs(file, &sfs);
#endif

	fsname = sfs.f_fstypename;

	/* extattr_list_link() does not return the series of keys NUL
	 * separated, as documented in the man page.  Instead, it 
	 * returns things DNS style, with a 1 byte length followed by
	 * the key, repeated for as many keys as there are.
	 */
	for( i=0; i < ret; i++ ) {
		char key[256];
		char *ns;
		char tempnam[1024];

		memset(key, 0, sizeof(key));
		memcpy(key, buf+i+1, buf[i]);
		i += buf[i] ;

		extattr_namespace_to_string(namespace, &ns);
		memset(tempnam, 0, sizeof(tempnam));
		snprintf(tempnam, sizeof(tempnam)-1, "%s/%s.%s", XAR_EA_FORK, ns, key);
		FBSDATTR_CONTEXT(&context)->ns = namespace;
		FBSDATTR_CONTEXT(&context)->file = file;
		FBSDATTR_CONTEXT(&context)->attr = key;

		xar_attr_set(f, tempnam, "fstype", fsname);
		xar_attrcopy_to_heap(x, f, tempnam, xar_fbsdattr_read, &context);

		free(FBSDATTR_CONTEXT(&context)->buf);
		FBSDATTR_CONTEXT(&context)->buf = NULL;
		FBSDATTR_CONTEXT(&context)->off = 0;
	}

	if( namespace == EXTATTR_NAMESPACE_USER ) {
		namespace = EXTATTR_NAMESPACE_SYSTEM;
		free(buf);
		goto TRYAGAIN;
	}

BAIL:
	free(buf);
	return ret;
#else
	return 0;
#endif
}

int32_t xar_fbsdattr_extract(xar_t x, xar_file_t f, const char* file, char *buffer, size_t len)
{
#ifdef HAVE_SYS_EXTATTR_H
	char *fsname = "bogus";
	const char *prop;
#if defined(HAVE_STATVFS) && defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
	struct statvfs sfs;
#else
	struct statfs sfs;
#endif
	int eaopt = 0;
	xar_iter_t iter;
	struct _fbsdattr_context context;
	
	memset(&context,0,sizeof(struct _fbsdattr_context));
	
	/* no fbsdattr attributes for data to a buffer */
	if(len){
		return 0;
	}
	
#if defined(HAVE_STATVFS) && defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
	statvfs(file, &sfs);
#else
	statfs(file, &sfs);
#endif
	fsname = sfs.f_fstypename;

	iter = xar_iter_new();
	for(prop = xar_prop_first(f, iter); prop; prop = xar_prop_next(iter)) {
		const char *tmp, *fs;

		if( strncmp(prop, XAR_EA_FORK, strlen(XAR_EA_FORK) != 0 ) )
			continue;
		if( strlen(prop) <= (strlen(XAR_EA_FORK)+1) )
			continue;
		fs = xar_attr_get(f, prop, "fstype");
		if( !eaopt && fs && strcmp(fs, fsname) != 0 ) {
			continue;
		}

		tmp = prop + strlen(XAR_EA_FORK) + 1;
		if( strstr(tmp, "/") )
			continue;
		if( strncmp(tmp, "user.", 5) == 0 ) {
			FBSDATTR_CONTEXT(&context)->ns = EXTATTR_NAMESPACE_USER;
			FBSDATTR_CONTEXT(&context)->attr = tmp + 5;
		} else if( strncmp(tmp, "system.", 7) == 0 ) {
			FBSDATTR_CONTEXT(&context)->ns = EXTATTR_NAMESPACE_SYSTEM;
			FBSDATTR_CONTEXT(&context)->attr = tmp + 7;
		} else {
			continue;
		}

		FBSDATTR_CONTEXT(&context)->file = file;
		xar_attrcopy_from_heap(x, f, prop, xar_fbsdattr_write, &context);
	}

	xar_iter_free(iter);

#endif
	return 0;
}
