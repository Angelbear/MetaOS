/*
 * Public cpktool header file
 * Copyright (C) 2009 - 2010 Zhi Guan <guan@pku.edu.cn>
 */

#ifndef HEADER_CPKTOOL_H
#define HEADER_CPKTOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define CPKTOOL_VERSION	"1.0"

/*
 * Example Values:
 *
 * user_home = 
 *	"/home/username" in UNIX
 *	"/sdcard" in Android
 *	"C:\Documents and Settings\username" in Windows XP
 *	"C:\Users\username" in Windows Vista/7
 * prog_home = ".cpk";
 * prog_name = "cpk"
 */
int cpktool_init(const char *user_home, const char *prog_home, char *prog_name,
	FILE *err_fp);
void cpktool_exit(void);
int cpktool_setup(const char *domainid);
int cpktool_import_params(const char *file);
int cpktool_export_params(const char *file);
int cpktool_print_params(const char *file);
int cpktool_genkey(const char *id, const char *file, const char *pass);
int cpktool_print_key(const char *file, const char *pass);
int cpktool_set_identity(const char *id);
char *cpktool_get_identity(void);
int cpktool_import_sign_key(const char *file, const char *pass);
int cpktool_import_decrypt_key(const char *file, const char *pass);
int cpktool_change_sign_password(const char *old_pass, const char *new_pass);
int cpktool_change_decrypt_password(const char *old_pass, const char *new_pass);
char *cpktool_sign_text(const char *text, int textlen, const char *pass);
int cpktool_verify_text(const char *text, int textlen, const char *signature,
	const char *signer);
char *cpktool_sign_file(const char *file, const char *pass);
int cpktool_verify_file(const char *file, const char *signature,
	const char *signer);
char *cpktool_encrypt_text(const char *text, int textlen, const char *id);
char *cpktool_decrypt_text(const char *text, int *outlen, const char *pass);
char *cpktool_envelope_encrypt_text(const char *text, int textlen, char **rcpts,
	int num_rcpts);
char *cpktool_envelope_decrypt_text(const char *text, int textlen, int *outlen,
	const char *pass);
int cpktool_envelope_encrypt_file(const char *infile, const char *outfile, 
	char **rcpts, int num_rcpts, int base64);
int cpktool_envelope_decrypt_file(const char *infile, const char *outfile,
	const char *pass);
int cpktool_format_preserve_sign_file(const char *infile, const char *outfile, 
	const char *pass);
int cpktool_format_preserve_verify_file(const char *file);
int cpktool_format_preserve_encrypt_file(const char *infile, const char *outfile,
	char **rcpts, int num_rcpts, int base64);
int cpktool_format_preserve_decrypt_file(const char *infile, const char *outfile,
	const char *pass);
int cpktool_test(void);


#ifdef __cplusplus
}
#endif
#endif
