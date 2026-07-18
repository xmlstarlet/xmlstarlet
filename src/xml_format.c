/*

XMLStarlet: Command Line Toolkit to query/edit/check/transform XML documents

Copyright (c) 2002 Mikhail Grushinskiy.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <config.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libxml/xmlmemory.h>
#include <libxml/debugXML.h>
#include <libxml/xmlIO.h>
#include <libxml/HTMLtree.h>
#include <libxml/xinclude.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xpointer.h>
#include <libxml/parserInternals.h>
#include <libxml/uri.h>
#include <libxml/xmlsave.h>

#include "xmlstar.h"

/*
 *  TODO:  1. Attribute formatting options (as every attribute on a new line)
 *         2. exit values on errors
 */

typedef struct _foOptions {
    int indent;               /* indent output */
    int indent_tab;           /* indent output with tab */
    int indent_spaces;        /* num spaces for indentation */
    int omit_decl;            /* omit xml declaration */
    int recovery;             /* try to recover what is parsable */
    int dropdtd;              /* remove the DOCTYPE of the input docs */
    int options;              /* global parsing flags */ 
#ifdef LIBXML_HTML_ENABLED
    int html;                 /* inputs are in HTML format */
#endif
    int quiet;                 /* quiet mode */
} foOptions;

typedef foOptions *foOptionsPtr;

const char *encoding = NULL;

/**
 *  Print small help for command line options
 */
void
foUsage(int argc, char **argv, exit_status status)
{
    extern void fprint_format_usage(FILE* o, const char* argv0);
    extern const char more_info[];
    FILE *o = (status == EXIT_SUCCESS)? stdout : stderr;
    fprint_format_usage(o, argv[0]);
    fprintf(o, "%s", more_info);
    exit(status);
}

/**
 *  Initialize global command line options
 */
void
foInitOptions(foOptionsPtr ops)
{
    ops->indent = 1;
    ops->indent_tab = 0;
    ops->indent_spaces = 2;
    ops->omit_decl = 0;
    ops->recovery = 0;
    ops->dropdtd = 0;
    ops->options = XML_PARSE_NONET | XML_PARSE_NOBLANKS;
#ifdef LIBXML_HTML_ENABLED
    ops->html = 0;
#endif
    ops->quiet = globalOptions.quiet;
}

/**
 *  Parse global command line options
 */
int
foParseOptions(foOptionsPtr ops, int argc, char **argv)
{
    int i;

    i = 2;
    while(i < argc)
    {
        if (!strcmp(argv[i], "--noindent") || !strcmp(argv[i], "-n"))
        {
            ops->indent = 0;
            i++;
        }
        else if (!strcmp(argv[i], "--encode") || !strcmp(argv[i], "-e"))
        {
            i++;
            encoding = argv[i];
            i++;
        }
        else if (!strcmp(argv[i], "--indent-tab") || !strcmp(argv[i], "-t"))
        {
            ops->indent_tab = 1;
            i++;
        }
        else if (!strcmp(argv[i], "--omit-decl") || !strcmp(argv[i], "-o"))
        {
            ops->omit_decl = 1;
            i++;
        }
        else if (!strcmp(argv[i], "--dropdtd") || !strcmp(argv[i], "-D"))
        {
            ops->dropdtd = 1;
            i++;
        }
        else if (!strcmp(argv[i], "--recover") || !strcmp(argv[i], "-R"))
        {
            ops->recovery = 1;
	    ops->options |= XML_PARSE_RECOVER;
            i++;
        }
        else if (!strcmp(argv[i], "--nocdata") || !strcmp(argv[i], "-C"))
        {
            ops->options |= XML_PARSE_NOCDATA;
	    i++;
        }
        else if (!strcmp(argv[i], "--nsclean") || !strcmp(argv[i], "-N"))
        {
            ops->options |= XML_PARSE_NSCLEAN;
	    i++;
        }
        else if (!strcmp(argv[i], "--indent-spaces") || !strcmp(argv[i], "-s"))
        {
            int value;
            i++;
            if (i >= argc) foUsage(argc, argv, EXIT_BAD_ARGS);
            if (sscanf(argv[i], "%d", &value) == 1)
            {
                if (value > 0) ops->indent_spaces = value;
            }
            else
            {
                foUsage(argc, argv, EXIT_BAD_ARGS);
            }
            ops->indent_tab = 0;
            i++;
        }
        else if (!strcmp(argv[i], "--quiet") || !strcmp(argv[i], "-Q"))
        {
            ops->quiet = 1;
            i++;
        }
#ifdef LIBXML_HTML_ENABLED
        else if (!strcmp(argv[i], "--html") || !strcmp(argv[i], "-H"))
        {
            ops->html = 1;
            i++;
        }
#endif
        else if (!strcmp(argv[i], "--net"))
        {
            ops->options &= ~XML_PARSE_NONET;
            i++;
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            foUsage(argc, argv, EXIT_SUCCESS);
        }
        else if (!strcmp(argv[i], "-"))
        {
            i++;
            break;
        }
        else if (argv[i][0] == '-')
        {
            foUsage(argc, argv, EXIT_BAD_ARGS);
        }
        else
        {
            i++;
            break;
        }
    }

    return i-1;
}

/**
 *  'process' xml document(s)
 */
int
foProcess(foOptionsPtr ops, int start, int argc, char **argv)
{
    int ret = 0;
    xmlDocPtr doc = NULL;
    char *fileName = "-";
    char *spaces = NULL;
    const char *indent = NULL;
    xmlSaveCtxt *save;
    const char *save_enc;
    int save_opts;

    if ((start > 1) && (start < argc) && (argv[start][0] != '-') &&
        strcmp(argv[start-1], "--indent-spaces") &&
        strcmp(argv[start-1], "-s"))
    {
        fileName = argv[start];   
    }
/*
    if (ops->recovery)
    {
        doc = xmlRecoverFile(fileName);
    }
    else    
*/
    if (ops->quiet)
        suppressErrors();

#ifdef LIBXML_HTML_ENABLED
    if (ops->html)
    {
        doc = xmlstarHtmlReadFile(fileName, NULL, ops->options);
    }
    else
#endif
        doc = xmlstarReadFile(fileName, NULL, ops->options);

    if (doc == NULL)
    {
        return 2;
    }

    /*
     * Remove DOCTYPE nodes
     */
    if (ops->dropdtd) {
        xmlDtdPtr dtd;

        dtd = xmlGetIntSubset(doc);
        if (dtd != NULL) {
            xmlUnlinkNode((xmlNodePtr)dtd);
            xmlFreeDtd(dtd);
        }
    }

    save_opts = XML_SAVE_FORMAT;
    if (ops->omit_decl)
        save_opts |= XML_SAVE_NO_DECL;

    if (ops->indent) {
        if (ops->indent_tab)
        {
            indent = "\t";
        }
        else if (ops->indent_spaces > 0)
        {
            spaces = xmlMalloc(ops->indent_spaces + 1);
            indent = spaces;
            memset(spaces, ' ', ops->indent_spaces);
            spaces[ops->indent_spaces] = '\0';
        }
#if LIBXML_VERSION >= 21400
        save_opts |= XML_SAVE_INDENT;
#else
        xmlIndentTreeOutput = 1;
        if (indent != NULL)
            xmlTreeIndentString = indent;
#endif
    } else {
#if LIBXML_VERSION >= 21400
        save_opts |= XML_SAVE_NO_INDENT;
#else
        xmlIndentTreeOutput = 0;
#endif
    }

    if (encoding != NULL)
        save_enc = encoding;
    else
        save_enc = (const char *) doc->encoding;
    save = xmlSaveToFd(/* STDOUT_FILENO */ 1, save_enc, save_opts);

#if LIBXML_VERSION >= 21400
    if (indent != NULL)
        xmlSaveSetIndentString(save, indent);
#endif

    xmlSaveDoc(save, doc);
    xmlSaveClose(save);

    free(spaces);
    xmlFreeDoc(doc);
    return ret;
}

/**
 *  This is the main function for 'format' option
 */
int
foMain(int argc, char **argv)
{
    int ret = 0;
    int start;
    static foOptions ops;

    if (argc <=1) foUsage(argc, argv, EXIT_BAD_ARGS);
    foInitOptions(&ops);
    start = foParseOptions(&ops, argc, argv);
    if (argc-start > 1) foUsage(argc, argv, EXIT_BAD_ARGS);
    ret = foProcess(&ops, start, argc, argv);
    
    return ret;
}
