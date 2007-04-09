/***************************************************************************
 *   Copyright (C) 2006 by Emmanuel B�ranger   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "audio/audio.h"

#include <alsa/asoundlib.h>
#include <QSound>

#if WORDS_BIGENDIAN
#define SwapLE16(x) ((((u_int16_t)x)<<8)|(((u_int16_t)x)>>8))
#define SwapLE32(x) ((((u_int32_t)x)<<24)|((((u_int32_t)x)<<8)&0x00FF0000) \
                     |((((u_int32_t)x)>>8)&0x0000FF00)|(((u_int32_t)x)>>24))
#else
#define SwapLE16(x) (x)
#define SwapLE32(x) (x)
#endif



#ifdef Q_OS_LINUX

#include <alsa/asoundlib.h>

#endif

#define		BUFFERSIZE   		1024


QAlsaSound::QAlsaSound( const QString& filename, QObject* parent):
	QSound(filename,parent) 
{

	Path = filename;
	is_available = initialise();

}



bool QAlsaSound::initialise()
{

	if (QSound::isAvailable())
		return TRUE ; 

#ifdef Q_OS_LINUX
	char   buffer [ BUFFERSIZE ] ;
	
	device = strdup("default");  /* playback device */
	char*   ptr ;
	u_long  databytes ;
	snd_pcm_format_t format;
	snd_pcm_hw_params_t *params;

        int err;

	/*
	 * Open the file for reading:
	 */
	if ( (fd = open(Path.toLatin1().constData(),O_RDONLY)) < 0 ) {
		qDebug("Error Opening WAV file %s\n",Path.toLatin1().constData());
		return FALSE;
	}

	if ( lseek(fd,0L,SEEK_SET) != 0L ) {
		qDebug("Error nRewinding WAV file %s\n",Path.toLatin1().constData());
		return FALSE;		/* Wav file must be seekable device */
	}


	read (fd, buffer, BUFFERSIZE) ;

	if (findchunk (buffer, "RIFF", BUFFERSIZE) != buffer) {
		qDebug("Bad format: Cannot find RIFF file marker\n");	/* wwg: Report error */
		return  FALSE ;
	}

	if (! findchunk (buffer, "WAVE", BUFFERSIZE)) {
		qDebug("Bad format: Cannot find WAVE file marker\n");	/* wwg: report error */
		return  FALSE ;
	}

	ptr = findchunk (buffer, "fmt ", BUFFERSIZE) ;

	if (! ptr) {
		qDebug("Bad format: Cannot find 'fmt' file marker\n");	/* wwg: report error */
		return  FALSE ;
	}

	ptr += 4 ;	/* Move past "fmt ".*/
	memcpy (&waveformat, ptr, sizeof (WAVEFORMAT)) ;
	waveformat.dwSize = SwapLE32(waveformat.dwSize);
	waveformat.wFormatTag = SwapLE16(waveformat.wFormatTag) ;
	waveformat.wChannels = SwapLE16(waveformat.wChannels) ;
	waveformat.dwSamplesPerSec = SwapLE32(waveformat.dwSamplesPerSec) ;
	waveformat.dwAvgBytesPerSec = SwapLE32(waveformat.dwAvgBytesPerSec) ;
	waveformat.wBlockAlign = SwapLE16(waveformat.wBlockAlign) ;
	waveformat.wBitsPerSample = SwapLE16(waveformat.wBitsPerSample) ;

	
	ptr = findchunk (buffer, "data", BUFFERSIZE) ;

	if (! ptr) {
		qDebug("Bad format: unable to find 'data' file marker\n");	/* wwg: report error */
		return  FALSE ;
	}

	ptr += 4 ;	/* Move past "data".*/
	memcpy (&databytes, ptr, sizeof (u_long)) ;

	samples    = databytes / waveformat.wBlockAlign ;
	datastart  = ((u_long) (ptr + 4)) - ((u_long) (&(buffer[0]))) ;

	switch (waveformat.wBitsPerSample)
	{
		case 8:
			format = SND_PCM_FORMAT_U8 ;
			break;
		case 16:	
			format = SND_PCM_FORMAT_S16_LE ;
			break;
		case 32 :
			format = SND_PCM_FORMAT_S32_LE;
			break;
		default :
			qDebug("Bad format: %i bits per seconds\n",waveformat.wBitsPerSample );	/* wwg: report error */
			return  FALSE ;
			break;
	}

	//fprintf(stdout,"%s - format :%d, %i Hz, %i channels \n",Path.latin1(),waveformat.wBitsPerSample, waveformat.dwSamplesPerSec, waveformat.wChannels);

	/*
	 *	ALSA pain
	 */
	snd_pcm_hw_params_alloca(&params);
  	

	if ((err = snd_pcm_open (&handle, device, SND_PCM_STREAM_PLAYBACK,SND_PCM_ASYNC)) < 0) {
                        qDebug("cannot open audio device %s (%s)\n", 
                                 device,
                                 snd_strerror (err));
                        return FALSE;
                }

	if ((err = snd_pcm_nonblock(handle, 1))< 0) {
			qDebug("Audio : nonblock setting error: %s", snd_strerror(err));
			return FALSE;
		}
	

	/* Init hwparams with full configuration space */
	if (snd_pcm_hw_params_any(handle, params) < 0) {
		qDebug("Audio : Can not configure this PCM device.\n");
		return FALSE;
		}


	err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		qDebug("Audio : Access type RW_INTERLEAVED not available");
		return FALSE;
	}

	err = snd_pcm_hw_params_set_format(handle, params, format);
	if (err < 0) {
		qDebug("Sample format non available");
		return FALSE;
	}

	err = snd_pcm_hw_params_set_channels(handle, params, waveformat.wChannels);
	if (err < 0) {
		qDebug("Channels count %i non available", waveformat.wChannels);
		return FALSE;
	}

	err = snd_pcm_hw_params_set_rate_near(handle, params, &waveformat.dwSamplesPerSec, 0);
	if (err < 0) {
		qDebug("Unable to set rate : %d", waveformat.dwSamplesPerSec);
		return FALSE;
	}

	assert(err >= 0);	

	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		qDebug("Unable to install hw params:");
		return FALSE;
	}

	chunk_size = 0;
	buffer_size=0;
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	snd_pcm_hw_params_get_period_size(params, &chunk_size, 0);
	bits_per_sample = snd_pcm_format_physical_width(format);
	bits_per_frame = bits_per_sample * waveformat.wChannels;
	chunk_bytes = chunk_size * bits_per_frame / 8;
	
	return TRUE ;

#endif

}



void QAlsaSound::play()
{

	if (!is_available)
		return;

#ifdef Q_OS_LINUX

	int err;
	/* 
	 * start playback
	 */ 

	err=lseek(fd,datastart,SEEK_SET);

	//int written;
	int count,f;
	char *buffer2;
	buffer2 = (char *)malloc (buffer_size);
        while ((count = read (fd, buffer2,buffer_size))) 
	{
		f=count*8/bits_per_frame;
		while ((frames = snd_pcm_writei(handle, buffer2, f)) < 0) 
        		snd_pcm_prepare(handle);
/*		written=0;
		while (f > 0) {

			frames = snd_pcm_writei(handle, buffer2+written, f);
                	if (frames == -EAGAIN || (frames >= 0 && frames < f)) 
				snd_pcm_wait(handle, 1000);
			else if (frames < 0)//{
				frames = snd_pcm_recover(handle, frames, 0);
				//snd_pcm_prepare(handle);//}
			if (frames < 0) {
				printf("snd_pcm_writei failed: %s\n", snd_strerror(err));
				break;
			}

			if (frames > 0)
			{
				f -= frames;
				written += frames * bits_per_frame / 8;
			}	
		}
	*/
        }
	snd_pcm_drain(handle);

#else

	QSound::play();
#endif

}

char* QAlsaSound::findchunk  (char* pstart, char* fourcc, size_t n)
{	char	*pend ;
	int		k, test ;

	pend = pstart + n ;

	while (pstart < pend)
	{ 	if (*pstart == *fourcc)       /* found match for first char*/
		{	test = TRUE ;
			for (k = 1 ; fourcc [k] != 0 ; k++)
				test = (test ? ( pstart [k] == fourcc [k] ) : FALSE) ;
			if (test)
				return  pstart ;
			} ; /* if*/
		pstart ++ ;
		} ; /* while lpstart*/

	return  NULL ;
}