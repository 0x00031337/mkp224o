
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "types.h"
#include "yaml.h"
#include "ioutil.h"
#include "base64.h"
#include "common.h"

#define LINEFEED_LEN       (sizeof(char))
#define NULLTERM_LEN       (sizeof(char))
#define PATH_SEPARATOR_LEN (sizeof(char))

static const char keys_field_generated[] = "---";
static const char keys_field_hostname[]  = "hostname: ";
static const char keys_field_publickey[] = "hs_ed25519_public_key: ";
static const char keys_field_secretkey[] = "hs_ed25519_secret_key: ";
static const char keys_field_time[]      = "time: ";

#define KEYS_FIELD_GENERATED_LEN (sizeof(keys_field_generated) - NULLTERM_LEN)
#define KEYS_FIELD_HOSTNAME_LEN  (sizeof(keys_field_hostname)  - NULLTERM_LEN)
#define KEYS_FIELD_PUBLICKEY_LEN (sizeof(keys_field_publickey) - NULLTERM_LEN)
#define KEYS_FIELD_SECRETKEY_LEN (sizeof(keys_field_secretkey) - NULLTERM_LEN)
#define KEYS_FIELD_TIME_LEN      (sizeof(keys_field_time)      - NULLTERM_LEN)

static const char hostname_example[] = "xxxxxvsjzke274nisktdqcl3eqm5ve3m6iur6vwme7m5p6kxivrvjnyd.onion";
static const char pubkey_example[]   = "PT0gZWQyNTUxOXYxLXB1YmxpYzogdHlwZTAgPT0AAAC973vWScqJr/GokqY4CXskGdqTbPIpH1bMJ9nX+VdFYw==";
static const char seckey_example[]   = "PT0gZWQyNTUxOXYxLXNlY3JldDogdHlwZTAgPT0AAACwCPMr6rvBRtkW7ZzZ8P7Ne4acRZrhPrN/EF6AETRraFGvdrkW5es4WXB2UxrbuUf8zPoIKkXK5cpdakYdUeM3";
static const char time_example[]     = "2018-07-04 21:31:20 Z";

#define HOSTNAME_LEN (sizeof(hostname_example) - NULLTERM_LEN)
#define PUBKEY_LEN   (sizeof(pubkey_example)   - NULLTERM_LEN)
#define SECKEY_LEN   (sizeof(seckey_example)   - NULLTERM_LEN)
#define TIME_LEN     (sizeof(time_example)     - NULLTERM_LEN)

#define KEYS_LEN ( \
	KEYS_FIELD_GENERATED_LEN + LINEFEED_LEN + \
	KEYS_FIELD_HOSTNAME_LEN + HOSTNAME_LEN + LINEFEED_LEN + \
	KEYS_FIELD_PUBLICKEY_LEN + PUBKEY_LEN + LINEFEED_LEN + \
	KEYS_FIELD_SECRETKEY_LEN + SECKEY_LEN + LINEFEED_LEN + \
	KEYS_FIELD_TIME_LEN + TIME_LEN + LINEFEED_LEN \
)

static pthread_mutex_t tminfo_mutex;

void yamlout_init()
{
	pthread_mutex_init(&tminfo_mutex,0);
}

void yamlout_clean()
{
	pthread_mutex_destroy(&tminfo_mutex);
}

#define BUF_APPEND(buf,offset,src,srclen) \
do { \
	memcpy(&buf[offset],(src),(srclen)); \
	offset += (srclen); \
} while (0)
#define BUF_APPEND_CSTR(buf,offset,src) BUF_APPEND(buf,offset,src,strlen(src))
#define BUF_APPEND_CHAR(buf,offset,c) buf[offset++] = (c)

void yamlout_writekeys(const char *hostname,const u8 *formated_public,const u8 *formated_secret)
{
	char keysbuf[KEYS_LEN];
	char pubkeybuf[PUBKEY_LEN + NULLTERM_LEN];
	char seckeybuf[SECKEY_LEN + NULLTERM_LEN];
	char timebuf[TIME_LEN + NULLTERM_LEN];
	size_t offset = 0;

	BUF_APPEND(keysbuf,offset,keys_field_generated,KEYS_FIELD_GENERATED_LEN);
	BUF_APPEND_CHAR(keysbuf,offset,'\n');

	BUF_APPEND(keysbuf,offset,keys_field_hostname,KEYS_FIELD_HOSTNAME_LEN);
	BUF_APPEND(keysbuf,offset,hostname,ONION_LEN);
	BUF_APPEND_CHAR(keysbuf,offset,'\n');

	BUF_APPEND(keysbuf,offset,keys_field_publickey,KEYS_FIELD_PUBLICKEY_LEN);
	base64_to(pubkeybuf,formated_public,FORMATTED_PUBLIC_LEN);
	BUF_APPEND(keysbuf,offset,pubkeybuf,PUBKEY_LEN);
	BUF_APPEND_CHAR(keysbuf,offset,'\n');

	BUF_APPEND(keysbuf,offset,keys_field_secretkey,KEYS_FIELD_SECRETKEY_LEN);
	base64_to(seckeybuf,formated_secret,FORMATTED_SECRET_LEN);
	BUF_APPEND(keysbuf,offset,seckeybuf,SECKEY_LEN);
	BUF_APPEND_CHAR(keysbuf,offset,'\n');

	BUF_APPEND(keysbuf,offset,keys_field_time,KEYS_FIELD_TIME_LEN);

	time_t currtime;
	time(&currtime);
	struct tm *tm_info;

	pthread_mutex_lock(&tminfo_mutex);
	tm_info = gmtime(&currtime);
	strftime(timebuf,sizeof(timebuf),"%Y-%m-%d %H:%M:%S Z",tm_info);
	pthread_mutex_unlock(&tminfo_mutex);

	BUF_APPEND(keysbuf,offset,timebuf,TIME_LEN);
	BUF_APPEND_CHAR(keysbuf,offset,'\n');

	assert(offset == KEYS_LEN);

	pthread_mutex_lock(&fout_mutex);
	fwrite(keysbuf,sizeof(keysbuf),1,fout);
	fflush(fout);
	pthread_mutex_unlock(&fout_mutex);
}

#undef BUF_APPEND_CHAR
#undef BUF_APPEND_CSTR
#undef BUF_APPEND

// pseudo YAML parser
int yamlin_parseandcreate(FILE *fin,char *sname,const char *hostname)
{
	char line[256];
	size_t len;
	u8 pubbuf[FORMATTED_PUBLIC_LEN];
	u8 secbuf[FORMATTED_SECRET_LEN];
	int hashost = 0,haspub = 0,hassec = 0,skipthis = 0;
	enum keytype { HOST, PUB, SEC } keyt;

	while (!feof(fin) && !ferror(fin)) {
		if (!fgets(line,sizeof(line),fin))
			break;
		
		len = strlen(line);
		
		// trim whitespace from the end
		while (len != 0 && (line[len-1] == ' ' || line[len-1] == '\n' || line[len-1] == '\r'))
			line[--len] = '\0';
		
		// skip empty lines
		if (len == 0)
			continue;
		
		if (len >= 3 && line[0] == '-' && line[1] == '-' && line[2] == '-') {
			// end of document indicator
			if (!skipthis && (hashost || haspub || hassec)) {
				fprintf(stderr,"ERROR: incomplete record\n");
				return 1;
			}
			hashost = haspub = hassec = skipthis = 0;
			continue;
		}
		
		if (skipthis)
			continue;
		
		char *start = line;
		// trim whitespace
		while (len != 0 && *start == ' ') {
			++start;
			--len;
		}
		// find ':'
		char *p = start;
		for (;*p != '\0';++p) {
			if (*p == ':') {
				*p++ = '\0';
				goto foundkey;
			}
		}
		// not `key: value`
		fprintf(stderr,"ERROR: invalid syntax\n");
		return 1; // XXX could continue too there but eh

	foundkey:

		if (!strcmp(start,"hostname"))
			keyt = HOST;
		else if (!strcmp(start,"hs_ed25519_public_key"))
			keyt = PUB;
		else if (!strcmp(start,"hs_ed25519_secret_key"))
			keyt = SEC;
		else
			continue; // uninterested

		// skip WS
		while (*p == ' ')
			++p;
		if (*p == '!') {
			// skip ! tag
			while (*p != '\0' && *p != ' ')
				++p;
			// skip WS
			while (*p == ' ')
				++p;
		}
		len = strlen(p);
		switch (keyt) {
			case HOST:
				if (len != ONION_LEN) {
					fprintf(stderr,"ERROR: invalid hostname syntax\n");
					return 1;
				}
				if (!hostname || !strcmp(hostname,p)) {
					memcpy(&sname[direndpos],p,len + 1);
					hashost = 1;
				} else
					skipthis = 1;

				break;
			case PUB:
				if (len != PUBKEY_LEN || !base64_valid(p,0)) {
					fprintf(stderr,"ERROR: invalid pubkey syntax\n");
					return 1;
				}
				base64_from(pubbuf,p,len);
				haspub = 1;
				break;
			case SEC:
				if (len != SECKEY_LEN || !base64_valid(p,0)) {
					fprintf(stderr,"ERROR: invalid seckey syntax\n");
					return 1;
				}
				base64_from(secbuf,p,len);
				hassec = 1;
				break;
		}
		if (hashost && haspub && hassec) {
			if (createdir(sname,1) != 0) {
				fprintf(stderr,"ERROR: could not create directory for key output\n");
				return 1;
			}

			strcpy(&sname[onionendpos],"/hs_ed25519_secret_key");
			writetofile(sname,secbuf,FORMATTED_SECRET_LEN,1);

			strcpy(&sname[onionendpos],"/hs_ed25519_public_key");
			writetofile(sname,pubbuf,FORMATTED_PUBLIC_LEN,0);

			strcpy(&sname[onionendpos],"/hostname");
			FILE *hfile = fopen(sname,"w");
			sname[onionendpos] = '\n';
			if (hfile) {
				fwrite(&sname[direndpos],ONION_LEN + 1,1,hfile);
				fclose(hfile);
			}
			if (fout) {
				fwrite(&sname[printstartpos],printlen,1,fout);
				fflush(fout);
			}
			if (hostname)
				return 0; // finished
			skipthis = 1;
		}
	}
	
	if (!feof(fin)) {
		fprintf(stderr,"error while reading input\n");
		return 1;
	}
	
	if (hostname) {
		fprintf(stderr,"hostname wasn't found in input\n");
		return 1;
	}
	
	return 0;
}
