/* httpd.c*/
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define LISTENADDR "0.0.0.0"

/* structures*/
struct sHttpRequest
{
    char method[8];
    char url[128];
};
typedef struct sHttpRequest httpreq;

struct sFile
{
    char filename[64];
    char *fc; /*file content*/
    int size;
};
typedef struct sFile File;

/*global*/
char *error;

/*returns 0 on error, or it returns a socket fd*/
int srv_init(int portno)
{
    int s;
    struct sockaddr_in srv;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        error = "socket() error";
        return 0;
    }
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(LISTENADDR);
    srv.sin_port = htons(portno);
    if (bind(s, (struct sockaddr *)&srv, sizeof(srv)) < 0)
    {
        close(s);
        error = "bind() error";
        return 0;
    }
    if (listen(s, 5) < 0)
    {
        close(s);
        error = "listen() error";
        return 0;
    }
    return s;
}
/*Returns 0 on error, or returns the nrw client's socket fd*/
int cli_accept(int s)
{
    int c;
    socklen_t addrlen;
    struct sockaddr_in cli;

    addrlen = 0;
    memset(&cli, 0, sizeof(cli));
    c = accept(s, (struct sockaddr *)&cli, &addrlen);
    if (c < 0)
    {
        error = "accept() error";
        return 0;
    }

    return c;
}

/*returns 0 on failure or it returns a httpreq structure */
httpreq *parse_http(char *str)
{
    httpreq *req;
    char *p;

    req = malloc(sizeof(httpreq));

    for (p = str; *p && *p != ' '; p++)
        ;
    if (*p == ' ')
    {
        *p = 0;
    }
    else
    {
        error = " parse_http() NOSPACE error";
        free(req);
        return 0;
    }
    strncpy(req->method, str, 7);

    for (str = ++p; *p && *p != ' '; p++)
        ;
    if (*p == ' ')
    {
        *p = 0;
    }
    else
    {
        error = " parse_http() NOSPACE error";
        free(req);
        return 0;
    }

    strncpy(req->url, str, 127);
    return req;
}

/* returns 0 on error or returns the data*/
char *cli_read(int c)
{
    static char buf[512];

    memset(buf, 0, 512);
    if (read(c, buf, 511) < 0)
    {
        error = "read() error";
        return 0;
    }
    return buf;
}
void http_headers(int c, int code)
{
    char buf[512];
    int n;
    memset(buf, 0, 512);
    snprintf(buf, 511,
             "HTTP/1.0 %d OK\n"
             "Server httpd.c\n"
             "Cache-Control: no-store, no-cache, max-age=0, private\n"
             "Content-Language: en\n"
             "Expires: -1\n"
             "X-Frame-Options: SAMEORIGIN\n",
             code);
    n = strlen(buf);
    write(c, buf, n);

    return;
}

void http_response(int c, char *contenttype, char *data)
{
    char buf[512];
    int n;
    memset(buf, 0, 512);
    n = strlen(data);
    snprintf(buf, 511,
             "Content-Type: %s\n"
             "Content-Length: %d\n"
             "\n %s\n",
             contenttype, n, data);
    n = strlen(buf);
    write(c, buf, n);

    return;
}

/*returns 0 on error or File structure on success*/
File *read_file(char *filename)
{
    char buf[512];
    char *p;
    int n, x, fd;
    File *f;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        return 0;
    }

    f = malloc(sizeof(struct sFile));
    if (!f)
    {
        close(fd);
        return 0;
    }

    strncpy(f->filename, filename, 63);
    f->fc = malloc(512);

    x = 0; /*bytes read*/
    while (1)
    {
        memset(buf, 0, 512);
        n = read(fd, buf, 512);
        if (!n)
        {
            break;
        }
        else if (n == -1)
        {
            error = "read() error";
            close(fd);
            free(f->fc);
            free(f);
            return 0;
        }

        memcpy(((f->fc) + x), buf, n);
        x += n;
        f->fc = realloc(f->fc, (512 + x));
    }
    f->size = x;
    close(fd);
    return f;
}

/*returns 0 on error and 1 on success*/
int sendfile(int c, char *contenttype, File *file)
{
    char buf[512];
    int n, x;
    char *p;

    if (!file)
    {
        return 0;
    }
    else
    {
        memset(buf, 0, 512);
        snprintf(buf, 511,
                 "Content-Type: %s\n"
                 "Content-Length: %d\n\n",
                 contenttype, file->size);
        n = strlen(buf);

        write(c, buf, n);
        n = file->size;
        p = file->fc;
        while (1)
        {
            x = write(c, p, (n < 512) ? n : 512);
            if (x < 1)
            {
                error = "write() error";
                return 0;
            }
            n -= x;
            if (n < 1)
            {
                break;
            }
            else
            {
                p += x;
            }
        }
        return 1;
    }
}
void cli_conn(int s, int c)
{
    httpreq *req;
    char buf[512];
    char *p;
    char *res;
    char str[129];
    File *f;

    p = cli_read(c);
    if (!p)
    {
        fprintf(stderr, "%s\n", error);
        close(c);
        return;
    }
    req = parse_http(p);
    if (!req)
    {
        fprintf(stderr, "%s\n", error);
        close(c);
        return;
    }
    /* TODO: improve security by checking ofr things like "../.." etc.*/
    if (!strcmp(req->method, "GET") && !strncmp(req->url, "/img/", 5))
    {
        if (strstr(req->url, ".."))
        {
            http_headers(c, 300);
            http_response(c, "text/plain", "Invalid usage");
        }
        memset(str, 0, 128);
        snprintf(str, 129, ".%s", req->url);
        printf("reading file: %s\n", str);
        f = read_file(str);
        if (!f)
        {
            res = "File not found";
            http_headers(c, 404);
            http_response(c, "text/plain", res);
        }
        else
        {
            http_headers(c, 200);
            if (!sendfile(c, "image/png", f))
            {
                printf("test\n");
                res = "HTTP server error";
                http_response(c, "text/plain", res);
                free(f->fc);
                free(f);
            }
        }
    }
    else if (!strcmp(req->method, "GET") && (!strcmp(req->url, "/app/webpage")))
    {
        res = "<html>Hello <img src=\"/img/test.png\" alt=\"image\"></img></html>";
        http_headers(c, 200);
        http_response(c, "text/html", res);
    }
    else
    {
        res = "File not found";
        http_headers(c, 404);
        http_response(c, "text/plain", res);
    }
    free(req);
    close(c);
    return;
}
int main(int argc, char *argv[])
{
    int s, c;
    char *port;
    char *template;

    if (argc < 2)
    {
        fprintf(stderr, "Usagee %s <listeting port>\n", argv[0]);
        return -1;
    }
    else
    {
        port = argv[1];
        s = srv_init(atoi(port));
    }
    if (!s)
    {
        fprintf(stderr, "%s/n", error);
        return -1;
    }

    printf("Listeting on %s:%s\n", LISTENADDR, port);
    while (1)
    {
        c = cli_accept(s);
        if (!c)
        {
            fprintf(stderr, "%s\n", error);
            continue;
        }
        printf("Incoming connection\n");
        if (!fork())
        {
            cli_conn(s, c);
        }

        /* for the main process: return the new process' id
         * for the new process return: 0
         * */
    }
    return -1;
}