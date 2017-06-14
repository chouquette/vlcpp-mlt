/*****************************************************************************
 * VLCProducer.cpp: Implementation for vlc producer
 *****************************************************************************
 * Copyright (C) 2008-2016 Yikei Lu
 *
 * Authors: Yikei Lu    <luyikei.qmltu@gmail.com>
 * Thanks:  Pawel Golinski <golpaw1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/



#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>

#include <mlt++/MltProfile.h>
#include <mlt++/MltProducer.h>

#include <vlcpp/vlc.hpp>

class VLCProducer
{
public:
    VLCProducer( mlt_profile profile, char* file, mlt_producer parent = nullptr )
        : m_parent( nullptr )
        , m_isValid( false )
        , m_audioIndex( -1 )
        , m_videoIndex( -1 )
        , m_lastPosition( -1 )
        , m_audioExpected( 0 )
        , m_videoExpected( 0 )
        , m_isAudioFrameReady( false )
        , m_isVideoFrameReady( false )
        , m_isAudioTooManyFrames( false )
        , m_isVideoTooManyFrames( false )
    {
        if ( !file )
            return;

        if ( parent == nullptr )
            parent = new mlt_producer_s;
        if ( mlt_producer_init( parent, this ) == 0 )
        {
            m_parent.reset( new Mlt::Producer( parent ) );
            m_parent->dec_ref();

            parent->get_frame = producer_get_frame;
            parent->close = ( mlt_destructor ) producer_close;

            m_parent->set_lcnumeric( "C" );
            m_parent->set( "resource", file );
            m_parent->set( "_profile", ( void* ) profile, 0, NULL, NULL );

            m_media = VLC::Media( instance, std::string( file ), VLC::Media::FromType::FromLocation );
            m_media.parseWithOptions( VLC::Media::ParseFlags::Local, 3000 );
            while ( m_media.parsedStatus() != VLC::Media::ParsedStatus::Done );
            if ( m_media.parsedStatus() == VLC::Media::ParsedStatus::Done )
            {
                auto tracks = m_media.tracks();
                m_parent->set( "meta.media.nb_streams", ( int ) tracks.size() );
                int i = 0;
                char key[200];
                for ( const auto& track : tracks )
                {
                    if ( track.type() == VLC::MediaTrack::Video )
                    {
                        if ( m_videoIndex == -1 )
                            m_videoIndex = i;

                        snprintf( key, sizeof(key), "meta.media.%d.stream.type", i );
                        m_parent->set( key, "video" );

                        snprintf( key, sizeof(key), "meta.media.%d.stream.frame_rate", i );
                        m_parent->set( key, ( double ) track.fpsNum() / track.fpsDen() );
                        snprintf( key, sizeof(key), "meta.media.%d.stream.frame_rate_num", i );
                        m_parent->set( key, ( int64_t ) track.fpsNum() );
                        snprintf( key, sizeof(key), "meta.media.%d.stream.frame_rate_den", i );
                        m_parent->set( key, ( int64_t ) track.fpsDen() );

                        snprintf( key, sizeof(key), "meta.media.%d.codec.frame_rate", i );
                        m_parent->set( key, ( double ) track.fpsNum() / track.fpsDen() );
                        snprintf( key, sizeof(key), "meta.media.%d.codec.frame_rate_num", i );
                        m_parent->set( key, ( int64_t ) track.fpsNum() );
                        snprintf( key, sizeof(key), "meta.media.%d.codec.frame_rate_den", i );
                        m_parent->set( key, ( int64_t ) track.fpsDen() );

                        snprintf( key, sizeof(key), "meta.media.%d.stream.sample_aspect_ratio", i );
                        m_parent->set( key, ( double ) track.sarNum() / track.sarDen() );
                        snprintf( key, sizeof(key), "meta.media.%d.stream.frame_rate_num", i );
                        m_parent->set( key, ( int64_t ) track.sarNum() );
                        snprintf( key, sizeof(key), "meta.media.%d.stream.frame_rate_den", i );
                        m_parent->set( key, ( int64_t ) track.sarDen() );

                        snprintf( key, sizeof(key), "meta.media.%d.codec.sample_aspect_ratio", i );
                        m_parent->set( key, ( double ) track.sarNum() / track.sarDen() );
                        snprintf( key, sizeof(key), "meta.media.%d.codec.frame_rate_num", i );
                        m_parent->set( key, ( int64_t ) track.sarNum() );
                        snprintf( key, sizeof(key), "meta.media.%d.codex.frame_rate_den", i );
                        m_parent->set( key, ( int64_t ) track.sarDen() );

                        snprintf( key, sizeof(key), "meta.media.%d.codec.width", i );
                        m_parent->set( key, ( int64_t ) track.width() );
                        snprintf( key, sizeof(key), "meta.media.%d.codec.height", i );
                        m_parent->set( key, ( int64_t ) track.height() );
                    }
                    else if ( track.type() == VLC::MediaTrack::Audio )
                    {
                        if ( m_audioIndex == -1 )
                            m_audioIndex = i;

                        snprintf( key, sizeof(key), "meta.media.%d.stream.type", i );
                        m_parent->set( key, "audio" );
                        snprintf( key, sizeof(key), "meta.media.%d.codec.sample_rate", i );
                        m_parent->set( key, ( int64_t ) track.rate() );
                        snprintf( key, sizeof(key), "meta.media.%d.codec.channels", i );
                        m_parent->set( key, ( int64_t ) track.channels() );
                    }

                    snprintf( key, sizeof(key), "meta.media.%d.codec.fourcc", i );
                    m_parent->set( key, ( int64_t ) track.codec() );
                    snprintf( key, sizeof(key), "meta.media.%d.codec.original_fourcc", i );
                    m_parent->set( key, ( int64_t ) track.originalFourCC() );
                    snprintf( key, sizeof(key), "meta.media.%d.codec.bit_rate", i );
                    m_parent->set( key, ( int64_t ) track.bitrate() );
                    i++;
                }

                if ( m_videoIndex != -1 )
                {
                    m_parent->set( "width", ( int64_t ) tracks[m_videoIndex].width() );
                    m_parent->set( "meta.media.width", ( int64_t ) tracks[m_videoIndex].width() );
                    m_parent->set( "height", ( int64_t ) tracks[m_videoIndex].height() );
                    m_parent->set( "meta.media.height", ( int64_t ) tracks[m_videoIndex].height() );
                    m_parent->set( "height", ( int64_t ) tracks[m_videoIndex].height() );
                    m_parent->set( "meta.media.sample_aspect_num", ( int64_t ) tracks[m_videoIndex].sarNum() );
                    m_parent->set( "meta.media.sample_aspect_den", ( int64_t ) tracks[m_videoIndex].sarDen() );
                    m_parent->set( "aspect_ratio", ( double ) tracks[m_videoIndex].sarNum() / tracks[m_videoIndex].sarDen() );
                    m_parent->set( "meta.media.frame_rate_num", ( int64_t ) tracks[m_videoIndex].fpsNum() );
                    m_parent->set( "meta.media.frame_rate_den", ( int64_t ) tracks[m_videoIndex].fpsDen() );
                    m_parent->set( "frame_rate", ( double ) tracks[m_videoIndex].fpsNum() / tracks[m_videoIndex].fpsDen() );
                    m_parent->set( "length",
                                   ( int ) ( m_media.duration() * m_parent->get_fps() / 1000 + 0.5 ) );
                    m_parent->set( "out", ( int ) m_parent->get_int( "length" ) - 1 );
                }

                if ( m_audioIndex != -1 )
                {
                    m_parent->set( "meta.media.sample_rate", ( int64_t ) tracks[m_audioIndex].rate() );
                    m_parent->set( "meta.media.channels", ( int64_t ) tracks[m_audioIndex].channels() );
                    m_parent->set( "sample_rate", ( int64_t ) tracks[m_audioIndex].rate() );
                    m_parent->set( "channels", ( int64_t ) tracks[m_audioIndex].channels() );
                }

                char smem_options[ 1000 ];
                sprintf( smem_options,
                        ":sout=#transcode{"
                        "fps=%d/%d,"
                        "vcodec=%s,"
                        "acodec=%s,"
                        "}:smem{"
                        "time-sync,"
                        "no-sout-smem-time-sync,"
                        "audio-prerender-callback=%" PRIdPTR ","
                        "audio-postrender-callback=%" PRIdPTR ","
                        "video-prerender-callback=%" PRIdPTR ","
                        "video-postrender-callback=%" PRIdPTR ","
                        "audio-data=%" PRIdPTR ","
                        "video-data=%" PRIdPTR ","
                        "}",
                        profile->frame_rate_num,
                        profile->frame_rate_den,
                        "YUY2",
                        "s16l",
                        ( intptr_t ) &audio_lock,
                        ( intptr_t ) &audio_unlock,
                        ( intptr_t ) &video_lock,
                        ( intptr_t ) &video_unlock,
                        ( intptr_t ) this,
                        ( intptr_t ) this
                );

                m_media.addOption( smem_options );
                m_mediaPlayer = VLC::MediaPlayer( m_media );
                m_isValid = true;
            }
            mlt_service_cache_put( MLT_PRODUCER_SERVICE( parent ), "vlcProducer", this, 0,
                                   ( mlt_destructor ) producer_close );
        }
    }

    mlt_producer producer()
    {
        return m_parent->get_producer();
    }

    bool isValid()
    {
        return m_isValid;
    }

    ~VLCProducer()
    {
        if ( m_mediaPlayer != nullptr )
            m_mediaPlayer.stop();
        clearFrames();
    }

    static VLC::Instance    instance;

private:

    struct Frame {
        ~Frame()
        {
            mlt_pool_release( buffer );
        }

        uint8_t* buffer;
        int size;
        int64_t vlcTime;
        int iterator;
    };

    static void audio_lock( void* data, uint8_t** buffer, size_t size )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( data );
        std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );

        if ( vlcProducer->m_audioFrames.size() >= 100 )
            vlcProducer->m_isAudioTooManyFrames = true;
        else
            vlcProducer->m_isAudioTooManyFrames = false;

        vlcProducer->m_cv2.wait( lck, [vlcProducer]{ return vlcProducer->m_isAudioTooManyFrames == false; } );

        *buffer = ( uint8_t* ) mlt_pool_alloc( size * sizeof( uint8_t ) );
    }

    static void audio_unlock( void* data, uint8_t* buffer, unsigned int channels,
                              unsigned int rate, unsigned int nb_samples, unsigned int bps,
                              size_t size, int64_t pts )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( data );

        std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );
        auto frame = std::make_shared<Frame>();
        frame->buffer = buffer;
        frame->size = size;
        frame->iterator = 0;
        frame->vlcTime = vlcProducer->m_mediaPlayer.time();

        vlcProducer->m_audioFrames.push_back( frame );
        vlcProducer->m_isAudioFrameReady = true;
        vlcProducer->m_cv.notify_all();
    }

    static void video_lock( void* data, uint8_t** buffer, size_t size )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( data );
        std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );

        if ( vlcProducer->m_videoFrames.size() >= 20 )
            vlcProducer->m_isVideoTooManyFrames = true;
        else
            vlcProducer->m_isVideoTooManyFrames = false;

        vlcProducer->m_cv2.wait( lck, [vlcProducer]{ return vlcProducer->m_isVideoTooManyFrames == false; } );

        *buffer = ( uint8_t* ) mlt_pool_alloc( size * sizeof( uint8_t ) );
    }

    static void video_unlock( void* data, uint8_t* buffer, int width, int height,
                              int bpp, size_t size, int64_t pts )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( data );

        std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );
        auto frame = std::make_shared<Frame>();
        frame->buffer = buffer;
        frame->size = size;
        frame->vlcTime = vlcProducer->m_mediaPlayer.time();

        vlcProducer->m_videoFrames.push_back( frame );
        vlcProducer->m_isVideoFrameReady = true;
        vlcProducer->m_cv.notify_all();
    }

    static void producer_close( mlt_producer parent )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( parent->child );
        vlcProducer->producer()->close = nullptr;
        mlt_service_cache_purge( vlcProducer->m_parent->get_service() );
    }

    static int producer_get_image( mlt_frame frame, uint8_t** buffer,
                                   mlt_image_format* format, int* width, int* height, int writable )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( mlt_frame_pop_service( frame ) );
        auto producer = vlcProducer->producer();

        if ( vlcProducer->m_videoFrames.size() > 0 )
            vlcProducer->m_isVideoFrameReady = true;
        else
            vlcProducer->m_isVideoFrameReady = false;
        std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );
        vlcProducer->m_cv.wait_for( lck, std::chrono::milliseconds( 1000 ),
                                    [vlcProducer]{ return vlcProducer->m_isVideoFrameReady; } );

        if ( vlcProducer->m_videoFrames.size() >= 20 )
            vlcProducer->m_isVideoTooManyFrames = true;
        else
            vlcProducer->m_isVideoTooManyFrames = false;
        vlcProducer->m_cv2.notify_all();

        *format = mlt_image_yuv422;
        *width = vlcProducer->m_parent->get_int( "width" );
        *height = vlcProducer->m_parent->get_int( "height" );


        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "format", mlt_image_yuv422 );
        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ),"width", vlcProducer->m_parent->get_int( "width" ) );
        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ),"height", vlcProducer->m_parent->get_int( "height" ) );

        vlcProducer->m_lastPosition = mlt_producer_position( producer );
        auto posDiff = vlcProducer->m_videoExpected - vlcProducer->m_lastPosition;
        bool toSeek = posDiff > 1 || posDiff <= -12;
        bool paused = posDiff == 1;

        uint8_t* newFrame = nullptr;
        size_t size;

        if ( vlcProducer->m_videoFrames.size() > 0 )
        {
            auto videoFrame = vlcProducer->m_videoFrames.front();
            size = videoFrame->size;
            newFrame = videoFrame->buffer;

            if ( paused == false )
            {
                videoFrame->buffer = nullptr;
                vlcProducer->m_videoFrames.pop_front();
            }
        }
        else
        {
            size = *width * *height * 4;
            newFrame = ( uint8_t* ) mlt_pool_alloc( size * sizeof( uint8_t ) );
        }

        *buffer = newFrame;
        mlt_frame_set_image( frame, newFrame, size,
                             ( mlt_destructor ) mlt_pool_release );

        if ( paused == false )
        {
            // Seek
            if ( toSeek )
            {
                vlcProducer->m_videoExpected = vlcProducer->m_lastPosition;
                vlcProducer->m_mediaPlayer.setTime( vlcProducer->m_lastPosition * 1000.0 / vlcProducer->m_parent->get_fps() + 0.5 );
                vlcProducer->clearFrames();
            }
        }

        vlcProducer->m_videoExpected = vlcProducer->m_lastPosition + 1;

        return 0;
    }

    static int producer_get_audio( mlt_frame frame, void** buffer, mlt_audio_format* format,
                                   int* frequency, int* channels, int* samples )
    {
        auto vlcProducer = reinterpret_cast<VLCProducer*>( mlt_frame_pop_audio( frame ) );

        if ( vlcProducer->m_audioFrames.size() > 0 )
            vlcProducer->m_isAudioFrameReady = true;
        else
            vlcProducer->m_isAudioFrameReady = false;

        std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );
        vlcProducer->m_cv.wait_for( lck, std::chrono::milliseconds( 1000 ),
                                    [vlcProducer]{ return vlcProducer->m_isAudioFrameReady; } );

        if ( vlcProducer->m_audioFrames.size() >= 100 )
            vlcProducer->m_isAudioTooManyFrames = true;
        else
            vlcProducer->m_isAudioTooManyFrames = false;
        vlcProducer->m_cv2.notify_all();


        double fps = vlcProducer->m_parent->get_fps();
        if ( mlt_properties_get( MLT_FRAME_PROPERTIES( frame ), "producer_consumer_fps" ) )
            fps = mlt_properties_get_double( MLT_FRAME_PROPERTIES(frame), "producer_consumer_fps" );

        int needed_samples = mlt_sample_calculator(
            fps,
            vlcProducer->m_parent->get_int64( "sample_rate" ),
            vlcProducer->m_lastPosition );

        int audio_buffer_size = mlt_audio_format_size( mlt_audio_s16, needed_samples,
                                                       vlcProducer->m_parent->get_int64( "channels" ) );

        auto packedAudioBuffer = ( uint8_t* ) mlt_pool_alloc( audio_buffer_size );\

        if ( vlcProducer->m_audioFrames.size() > 0 &&
             ( int ) ( ( vlcProducer->m_audioFrames.size() - 1 ) * vlcProducer->m_audioFrames[0]->size ) >= audio_buffer_size )
        {
            int iterator = 0;
            while ( iterator < audio_buffer_size )
            {
                auto frontBuffer = vlcProducer->m_audioFrames.front();

                while ( frontBuffer->iterator < frontBuffer->size && iterator < audio_buffer_size )
                    packedAudioBuffer[iterator++] = frontBuffer->buffer[frontBuffer->iterator++];

                if ( frontBuffer->iterator == frontBuffer->size )
                {
                    vlcProducer->m_audioFrames.pop_front();
                }
            }
        }

        mlt_frame_set_audio( frame, packedAudioBuffer, mlt_audio_s16,
                        audio_buffer_size, ( mlt_destructor ) mlt_pool_release );

        *buffer = packedAudioBuffer;
        *frequency = vlcProducer->m_parent->get_int64( "sample_rate" );
        *channels = vlcProducer->m_parent->get_int64( "channels" );
        *samples = needed_samples;
        *format = mlt_audio_s16;

        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ),
                                "audio_frequency", vlcProducer->m_parent->get_int64( "sample_rate" ) );
        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ),
                                "audio_channels", vlcProducer->m_parent->get_int64( "channels" ) );
        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "audio_samples", needed_samples );
        mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "audio_format", mlt_audio_s16 );

        vlcProducer->m_audioExpected++;

        return 0;
    }

    static int producer_get_frame( mlt_producer producer, mlt_frame_ptr frame, int index )
    {
        mlt_service service = MLT_PRODUCER_SERVICE( producer );
        mlt_cache_item cache_item = mlt_service_cache_get( service, "vlcProducer" );
        auto vlcProducer = reinterpret_cast<VLCProducer*>( mlt_cache_item_data( cache_item, NULL ) );

        if ( vlcProducer == nullptr )
        {
            vlcProducer = new VLCProducer(
                                mlt_service_profile( MLT_PRODUCER_SERVICE( producer ) ),
                                mlt_properties_get( MLT_PRODUCER_PROPERTIES( producer ), "resource" ),
                                producer );

            producer->child = vlcProducer;
            mlt_service_cache_put( service, "vlcProducer", vlcProducer, 0,
                                   ( mlt_destructor ) producer_close );
        }


        *frame = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );

        mlt_properties_set_position( MLT_FRAME_PROPERTIES( *frame ), "original_position", mlt_producer_frame( producer ) );
        mlt_producer_prepare_next( producer );
        {
            std::unique_lock<std::mutex> lck( vlcProducer->m_safeLock );
            if ( vlcProducer->m_mediaPlayer.isPlaying() == false )
                vlcProducer->m_mediaPlayer.play();
            mlt_frame_set_position( *frame, vlcProducer->m_lastPosition );
        }

        mlt_frame_push_service( *frame, vlcProducer );
        mlt_frame_push_get_image( *frame, producer_get_image );

        mlt_frame_push_audio( *frame, vlcProducer );
        mlt_frame_push_audio( *frame, (void*) producer_get_audio );

        return 0;
    }

    void clearFrames()
    {
        m_audioFrames.clear();
        m_videoFrames.clear();
    }

    std::unique_ptr<Mlt::Producer>      m_parent;

    VLC::Media          m_media;
    VLC::MediaPlayer    m_mediaPlayer;

    std::deque<std::shared_ptr<Frame>>  m_videoFrames;
    std::deque<std::shared_ptr<Frame>>  m_audioFrames;

    bool                m_isValid;

    int                 m_audioIndex;
    int                 m_videoIndex;
    int                 m_lastPosition;
    mlt_position        m_audioExpected;
    mlt_position        m_videoExpected;

    std::mutex          m_safeLock;
    bool                        m_isAudioFrameReady;
    bool                        m_isVideoFrameReady;
    bool                        m_isAudioTooManyFrames;
    bool                        m_isVideoTooManyFrames;
    std::condition_variable     m_cv;  // For m_isFrameReady
    std::condition_variable     m_cv2; // For m_isTooManyFrames
};


const char * const argv[] = {
    "--no-skip-frames",
    "--text-renderer",
    "--no-sub-autodetect-file",
    "--no-disable-screensaver",
    NULL,
};

VLC::Instance VLCProducer::instance = VLC::Instance( 4, argv );

extern "C" mlt_producer producer_vlc_init_CXX( mlt_profile profile, mlt_service_type type , const char* id , char* arg )
{
    auto vlcProducer = new VLCProducer( profile, arg );
    if ( vlcProducer->isValid() == true )
        return vlcProducer->producer();
    else
    {
        delete vlcProducer;
        return NULL;
    }
}
