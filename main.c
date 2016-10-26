/* ddst printer driver for ricoh afficio c240dn, and possibly other primark powered gdi laser printers
* Licensed under GPL v3
* Copyright Malte Sartor 2016
* Contact: msartor@gmail.com
*/
#include <cups/cups.h>
#include <cups/ppd.h>

#include <cups/raster.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <jbig.h>
#include <time.h>
#include <math.h>
//globals
signed char** error_diff_buf;
size_t last_jbig_len;
size_t gdib_len;
//prototypes
int band(unsigned char *plane, size_t len, unsigned int line,cups_page_header2_t *header);
int job_header(cups_page_header2_t *header);
int page_header(cups_page_header2_t *header, unsigned int page);
int halftone(unsigned char* pixels, size_t len, unsigned int line, cups_page_header2_t *header, unsigned char** bits);





int job_end();





unsigned int* halftones[4][256];
signed char gamut[256];
int cancel();



void append_jbig(unsigned char *start, size_t len, void *file)
{
    unsigned char* f=(unsigned char*)file;

    size_t i=0;
    for(i=0; i<len; i++)
    {
        f[gdib_len+i]=start[i];

    }
    gdib_len+=len;

    //memcpy(&f[gdib_len],start,len);
    return;
}

int job_end()
{

    signed char jidg[4]= {'J','I','D','G'};
    fwrite(jidg,sizeof(unsigned char),4,stdout);
    fflush(stdout);
    return 0;
}



int cancel()
{
    return 0;
}

//output the jobs header
int job_header(cups_page_header2_t *header)
{
    fputs("INFO: raster2ddst sending job header...", stderr);
    char* user = "tmpuser";
    char* station = "tmpstation";
    char* job = "tmpjob";
    char* gdij;
    gdij=calloc(120,sizeof(unsigned char));
    gdij[0]='G';
    gdij[1]='D';
    gdij[2]='I';
    gdij[3]='J';
    gdij[7]=120;//len
    gdij[9]=100;//unk, afaik always 100
    gdij[11]=1;
    gdij[17]=8;
    gdij[19]=168;
    uint32_t fuckeduptime;

    time_t t_;
    time(&t_);
    struct tm * t;
    t=localtime(&t_);

    fuckeduptime=t->tm_mday<<27|t->tm_mon<<23|((t->tm_year-80)&0xFF)<<16|(t->tm_sec/2)<<10|t->tm_min<<5|t->tm_hour;
    gdij[20]=(fuckeduptime>>24)&0xFF; //length
    gdij[21]=(fuckeduptime>>16)&0xFF;
    gdij[22]=(fuckeduptime>>8)&0xFF;
    gdij[23]=(fuckeduptime>>0)&0xFF;
    gdij[32]=1;
    gdij[35]=2;
    strcpy(&gdij[56],job);
    fwrite(gdij,sizeof(unsigned char),120,stdout);
    fflush(stdout);
    free(gdij);
    //now, to the gjet header...
    unsigned char* gjet;
    gjet=calloc(168,sizeof(unsigned char));
    gjet[0]='G';
    gjet[1]='J';
    gjet[2]='E';
    gjet[3]='T';
    gjet[7]=168;//len
    strcpy(&gjet[8],station);
    strcpy(&gjet[72],user);
    fwrite(gjet,sizeof(unsigned char),168,stdout);
    fflush(stdout);
    free(gjet);
    fputs("OK\n", stderr);

    return 0;


}

//generate and output a page header...
int page_header(cups_page_header2_t *header, unsigned int page)
{
    fputs("INFO: raster2ddst sedning page header...", stderr);
    unsigned char* gdip;
    gdip=calloc(64,sizeof(unsigned char));
    gdip[0]='G';
    gdip[1]='D';
    gdip[2]='I';
    gdip[3]='P';
    gdip[7]=64;
    switch (header->PageSize[1])//!TODO: ADD MISSING PAGE SIZES
    {
    case 540 : /* Monarch Envelope */
        break;

    case 624 : /* DL Envelope */
        break;

    case 649 : /* C5 Envelope */
        break;

    case 684 : /* COM-10 Envelope */
        break;

    case 709 : /* B5 Envelope */
        break;

    case 756 : /* Executive */
        break;

    case 792 : /* Letter */
        gdip[8]=1;
        break;

    case 842 : /* A4 */
        gdip[8]=9;
        break;

    case 1008 : /* Legal */
        break;

    case 1191 : /* A3 */
        break;

    case 1224 : /* Tabloid */
        break;
    }
    gdip[12]=(header->cupsWidth>>8)&0xff;
    gdip[13]=(header->cupsWidth>>0)&0xff;
    gdip[14]=(header->cupsHeight>>8)&0xff;
    gdip[15]=(header->cupsHeight>>0)&0xff;
    unsigned int print_um[2];
    fprintf(stderr,"cupsPageSize[0]: %f\n",header->cupsPageSize[0]);
    print_um[0]=(unsigned int)(header->cupsPageSize[0]*352.777777778f); // divide by 72, multiply by 25400 (points to um)
    print_um[1]=(unsigned int)(header->cupsPageSize[1]*352.777777778f);
    gdip[55]=header->cupsHeight/255;
    if(header->cupsHeight%255!=0)
        gdip[55]++;


    gdip[56]=(print_um[0]>>24) &0xff;
    gdip[57]=(print_um[0]>>16) &0xff;
    gdip[58]=(print_um[0]>>8) &0xff;
    gdip[59]=(print_um[0]>>0) &0xff;
    gdip[60]=(print_um[1]>>24) &0xff;
    gdip[61]=(print_um[1]>>16) &0xff;
    gdip[62]=(print_um[1]>>8) &0xff;
    gdip[63]=(print_um[1]>>0) &0xff;


    gdip[32]=(header->cupsColorSpace == CUPS_CSPACE_KCMY)?4:1;
    gdip[33]=header->Duplex?(page%2==0?0xd:5):1;
    fwrite(gdip,sizeof(unsigned char),64,stdout);
    fflush(stdout);
    free(gdip);
    fputs("OK\n", stderr);

    return 0;

}



//dither, compress and output a band
int band(unsigned char* pixels, size_t len,unsigned int line, cups_page_header2_t* header)
{
    //testing: fill first band with 100, 50 and 25% K, C, M and Y
    unsigned int numbands=header->cupsColorSpace==CUPS_CSPACE_KCMY?4:1;
    unsigned int num_lines=len/header->cupsBytesPerLine;
    unsigned int cline, ccol;
    unsigned int b,p;
    #ifdef testbar
    if(line== 0)
    {
        for(p=0; p<len/numbands-numbands; p++)
        {


            cline=p/header->cupsWidth;
            ccol=p%header->cupsWidth;

            if (ccol < 1 * header->cupsWidth/4)
            {
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+0]=255-255*(ccol-0)/(header->cupsWidth/4);
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+1] = 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+2] = 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+3] = 0;
            }
            else if (ccol < 2 * header->cupsWidth/4)
            {
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+0]= 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+1] = 255-255*(ccol-header->cupsWidth/4)/(header->cupsWidth/4);
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+2] = 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+3] = 0;
            }
            else if  (ccol < 3 * header->cupsWidth/4)
            {
                pixels[cline*header->cupsBytesPerLine+ccol*numbands+0]= 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+1] = 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+2] =255-255*(ccol-2*header->cupsWidth/4)/(header->cupsWidth/4);
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+3] = 0;


            }
            else if  (ccol < 4 * header->cupsWidth/4)
            {
                pixels[cline*header->cupsBytesPerLine+ccol*numbands+0]= 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+1] = 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+2] = 0;
               pixels[cline*header->cupsBytesPerLine+ccol*numbands+3] = 255-255*(ccol-2*header->cupsWidth/4)/(header->cupsWidth/4);


            }

        }
    }
    #endif




    fputs("INFO: raster2ddst processing band...\n", stderr);
    //output can never be larger than input, so malloc the length of the output here...
    unsigned char* gdib=calloc(len,sizeof(unsigned char));
    fputs("INFO: raster2ddst processing band...\n", stderr);
    gdib_len=32;

    fputs("INFO: raster2ddst processing band...\n", stderr);

    //table of diffused errors, order: kmcy, first all k and so on...
    double** diffused_errors= malloc(sizeof(double*)*numbands);
    unsigned char** bits=malloc(sizeof(signed char*)*numbands);

    for(b=0; b<numbands; b++)
    {
        bits[b]=calloc(len/numbands/8,sizeof(signed char));
        diffused_errors[b]=calloc(len/numbands+header->cupsWidth,sizeof(double));
    }


    //prefill diffused errors, clear error diff buff

    for(b=0; b<numbands; b++)
    {

        for(p=0; p<header->cupsWidth; p++)
        {
            //diffused_errors[b*color_offset+p]=error_diff_buf[b][p]; //overband diffusion disabled for now...
            error_diff_buf[b][p]=0;
        }
    }
    //dither into bits[][] with floyd steinberg, only one color per pixel possible...
    fputs("INFO: raster2ddst Dithering band...\n", stderr);



    //simple floating point sierra-2

    for(p=0; p<len/numbands-numbands; p++)
    {


        cline=p/header->cupsWidth;
        ccol=p%header->cupsWidth;


        double p_val[4]= {0.0,0.0,0.0,0.0};
        double e[4]= {0.0,0.0,0.0,0.0};
        double red[4] = {2.5,2.5,2.5,2.5};
        double norm=1.0;
        int highest_b = -1;
        double highest_val = -500.0;
        //normalize pixel
        double pixsum=0.0;


        for (b=0; b<numbands; b++)
        {
            pixsum+= (double)(pixels[cline*header->cupsBytesPerLine+ccol*numbands+b]);
        }
        if (pixsum > 255.0*numbands){
          norm=255.0*(double)numbands/pixsum;
             fprintf(stderr,"sum is weird...:%f\n",pixsum);
        }

          for (b=0; b<numbands; b++)
        {
            //  fprintf(stderr,"accessing p:%i of %i: %i\n",cline*header->cupsBytesPerLine+ccol*numbands+b, len,pixels[cline*header->cupsBytesPerLine+ccol*numbands+b]);

             p_val[b] =diffused_errors[b][p] + ( (double)(pixels[cline*header->cupsBytesPerLine+ccol*numbands+b])/red[b])*norm/*+ pixels[p*numbands+b]*/;
            if (p_val[b] > highest_val)
            {
                highest_val = p_val[b];
                highest_b = b;

            }
        }

       /*if (highest_b == 0)
        {

            if (p_val[0] > 127.0)
            {
                bits[0][p/8]=bits[highest_b][p/8]|(1<<(7-p%8));
                for (b=0; b<numbands; b++)
                {
                    e[b] = 0.0;//p_val[b] - 255.0;
                }
            }
            else
            {
                for (b=0; b<numbands; b++)
                {
                    e[b] = p_val[b]/red[b];
                }
            }

        }
        else
        {*/
            for (b=0; b<numbands; b++)
            {
                e[b]= p_val[b];
                if (p_val[b] > 127.0 && b==highest_b)
                {
                    e[b] -=(255.0);
                    //set bit...
                    bits[b][p/8]=bits[b][p/8]|(1<<(7-p%8));
                }



            }
        //}

        for (b=0; b<numbands; b++)
        {

            //if(line==0 && cline < 2)
            // fprintf(stderr,"v:%f, diff:%i, error:%f, ccol:%i, b:%i\n",diffused_errors[b][p] ,pixels[cline*header->cupsBytesPerLine+ccol*numbands+b],e[b], ccol, b);

            //add a bit of pseudorandomness
            e[b]+=(((double)(rand()%1000)) - 500.0 ) / 250.0/red[b];

            //distribute e
            e[b]/=16.0;
            diffused_errors[b][p+header->cupsWidth]+= e[b]*3.0;
            if ( (ccol + 1) < header->cupsWidth)
            {
                diffused_errors[b][p+1] += e[b] *4.0;
                diffused_errors[b][p+header->cupsWidth+1] += e[b] *2.0;
            }
            if ( (ccol + 2) < header->cupsWidth)
            {
                diffused_errors[b][p+2] += e[b] *3.0;
                diffused_errors[b][p+header->cupsWidth+2] += e[b];
            }

            if (ccol > 0)
            {
                diffused_errors[b][p+header->cupsWidth-1] += e[b] *2.0;

            }

            if (ccol > 1)
            {
                diffused_errors[b][p+header->cupsWidth-2] += e[b];

            }

        }
    }


    //move last line of diffused error into error_diff_buff
   /* {overband dithering disabled for now...}
   for (b=0; b<numbands; b++)
    {
      memcpy(error_diff_buf[b],)

    }
*/
    //now we have a dithered band for all avail colors... encode them with jbig and put them in gdib...

    struct jbg_enc_state se;

    //k
    size_t old_len=gdib_len;
    unsigned char *bitmaps[1] = { bits[0] };
    jbg_enc_init(&se, header->cupsWidth, num_lines, 1, bitmaps,
                 append_jbig, gdib);
    jbg_enc_layers(&se,0);
    jbg_enc_options(&se,JBG_ILEAVE|JBG_SMID,JBG_LRLTWO,256,0,0);
    jbg_enc_out(&se);
    while(gdib_len-old_len<51 || (gdib_len-old_len)%4!=0)
    {
        gdib_len++;
    }
    gdib[10]=((gdib_len-old_len) >> 8) & 0xFF;
    gdib[11]=(gdib_len-old_len) & 0xFF;

    if(numbands==4)
    {
        //y
        bitmaps[0] = bits[3];
        old_len=gdib_len;

        jbg_enc_init(&se, header->cupsWidth, num_lines, 1, bitmaps,
                     append_jbig, gdib);
        jbg_enc_layers(&se,0);
        jbg_enc_options(&se,JBG_ILEAVE|JBG_SMID,JBG_LRLTWO,256,0,0);
        jbg_enc_out(&se);
        while(gdib_len-old_len<51 || (gdib_len-old_len)%4!=0)
        {
            gdib_len++;
        }
        gdib[14]=((gdib_len-old_len) >> 8) & 0xFF;
        gdib[15]=(gdib_len-old_len) & 0xFF;

        //m
        bitmaps[0] =  bits[2] ;
        old_len=gdib_len;

        jbg_enc_init(&se, header->cupsWidth, num_lines, 1, bitmaps,
                     append_jbig, gdib);
        jbg_enc_layers(&se,0);
        jbg_enc_options(&se,JBG_ILEAVE|JBG_SMID,JBG_LRLTWO,256,0,0);
        jbg_enc_out(&se);
        while(gdib_len-old_len<51 || (gdib_len-old_len)%4!=0)
        {
            gdib_len++;
        }
        gdib[18]=((gdib_len-old_len) >> 8) & 0xFF;
        gdib[19]=(gdib_len-old_len) & 0xFF;


        //c
        bitmaps[0] =  bits[1] ;
        old_len=gdib_len;

        jbg_enc_init(&se, header->cupsWidth, num_lines, 1, bitmaps,
                     append_jbig, gdib);
        jbg_enc_layers(&se,0);
        jbg_enc_options(&se,JBG_ILEAVE|JBG_SMID,JBG_LRLTWO,256,0,0);
        jbg_enc_out(&se);
        while(gdib_len-old_len<51 || (gdib_len-old_len)%4!=0)
        {
            gdib_len++;
        }
        gdib[22]=((gdib_len-old_len) >> 8) & 0xFF;
        gdib[23]=(gdib_len-old_len) & 0xFF;

    }
    //now, finish it off with the last few setting
    gdib[0]='G';
    gdib[1]='D';
    gdib[2]='I';
    gdib[3]='B';
    if(line==0)
    {
        gdib[7]=1;
    }
    if(num_lines<256)
    {
        gdib[7]=2;
    }
    gdib[24]=(header->cupsWidth >> 8) & 0xFF;
    gdib[25]=(header->cupsWidth >> 0) & 0xFF;
    gdib[26]=(num_lines >> 8) & 0xFF;
    gdib[27]=(num_lines >> 0) & 0xFF;

    fwrite(gdib,sizeof(unsigned char),gdib_len,stdout);
    fflush(stdout);
    free(gdib);
    for (b=0; b<numbands; b++){
       free(bits[b]);
       free(diffused_errors[b]);
    }
    free(bits);
    free(diffused_errors);
    jbg_enc_free(&se);

    return 0;
}



int main(int  argc,				/* I - Number of command-line arguments */
         char *argv[])			/* I - Command-line arguments */
{
    fputs("INFO: raster2ddst starting up...\n", stderr);

    int			fd;		/* File descriptor */
    cups_raster_t		*ras;		/* Raster stream for printing */
    cups_page_header2_t	header;		/* Page header from file */

    unsigned int  page;
    unsigned char *pixelbuf;
    unsigned int printed=0;
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
    struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */
#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
    sigset(SIGTERM, cancel);
#elif defined(HAVE_SIGACTION)
    memset(&action, 0, sizeof(action));

    sigemptyset(&action.sa_mask);
    action.sa_handler = cancel;
    sigaction(SIGTERM, &action, NULL);
#else
    signal(SIGTERM, cancel);
#endif /* HAVE_SIGSET */

    /*
    * Check command-line...
    */

    if (argc < 6 || argc > 7)
    {
        /*
         * We don't have the correct number of arguments; write an error message
         * and return.
         */

        fputs("ERROR: raster2ddst job-id user title copies options [file]\n", stderr);
        return (1);
    }


    //open file or stram
    if (argc == 7)
    {
        if ((fd = open(argv[6], O_RDONLY)) == -1)
        {
            perror("ERROR: Unable to open raster file - ");
            sleep(1);
            return (1);
        }
    }
    else
        fd = 0;

    ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
    page=0;

    while(cupsRasterReadHeader2(ras,&header))
    {

        fputs("INFO: raster2ddst processing page...\n", stderr);

        //on first page, add page header, and malloc pixelbuf, malloc error diffusion connectors for the bands
        unsigned int numbands=1;
        if(header.cupsColorSpace==CUPS_CSPACE_KCMY)
        {
            numbands=4;
        }
        if(page==0)
        {

            printed=1;
            job_header(&header);
            pixelbuf=malloc(header.cupsBytesPerLine*1024);
            //allocaote error diffusion connectors between bands
            error_diff_buf = malloc( numbands*sizeof(unsigned char* ));
            int b;
            for(b=0; b<numbands; b++)
            {
                error_diff_buf[b]=malloc(header.cupsWidth*sizeof(unsigned char*));
                int i=0;
                //pre alloc the table for colors with value!=0; diffuses points better in even colored areas
                unsigned char prealloc=0;
                switch(b)
                {
                case 1:
                    prealloc=16;
                    break;
                case 2:
                    prealloc=-16;
                    break;
                case 3:
                    prealloc=32;


                }
                for(i=0; i<header.cupsWidth; i++)
                {
                    error_diff_buf[b][i]=prealloc;
                }
            }
            //fill gamut table
            int i=0;

            for(i=0; i<256; i++)
            {
                gamut[i]=pow(256,(float)i/256)-1;
            }

        }
        int len_read;
        page_header(&header,page);
        unsigned int l;
        for(l=0; l<header.cupsHeight; l+=256)
        {
            //read the next 255 lines of the page, or read the rest of the page
            fputs("Reading pixels...\n", stderr);
            fprintf(stderr,"header.cupsBytesPerLine:%i, header.cupsHeight:%i, l:%i\n",header.cupsBytesPerLine,header.cupsHeight,l);
            len_read=cupsRasterReadPixels(ras,pixelbuf,header.cupsBytesPerLine*((l+256<=header.cupsHeight)?256:(header.cupsHeight-l)));
            fputs("OK\n", stderr);
            //fwrite(pixelbuf,1,len_read, testfile);
            if(len_read<1)
            {
                break;
            }
            unsigned char test;





            test=pixelbuf[0];
            fprintf(stderr,"first:%i\n",test);



            test=pixelbuf[len_read-1];
            fprintf(stderr,"last:%i\n",test);

            band(pixelbuf,len_read,l,&header);


        }


        page++;
    }
    if(printed)
    {
        job_end();
        free( pixelbuf);
        //...actually, i'd need to free error_diff_buf here... but don't bother for now.
    }

    //fclose(testfile);

    return 0;
}
