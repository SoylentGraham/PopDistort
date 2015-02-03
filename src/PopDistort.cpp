#include "PopDistort.h"
#include <TParameters.h>
#include <SoyDebug.h>
#include <TProtocolCli.h>
#include <TProtocolHttp.h>
#include <SoyApp.h>
#include <PopMain.h>
#include <TJobRelay.h>
#include <SoyPixels.h>
#include <SoyString.h>
#include <TFeatureBinRing.h>
#include <SortArray.h>
#include <TChannelLiteral.h>





//	soypixels -> png data
template <> template<>
bool SoyData_Impl<std::string>::Encode(const SoyData_Impl<vec2f>& Data)
{
	std::stringstream vec2fstring;
	vec2fstring << Data.mValue.x << "," << Data.mValue.y;
	mValue = vec2fstring.str();

	OnEncode( Data.GetFormat() );
	return true;
}

void MakeAngles(ArrayBridge<float>&& AngleDegs,float From,float To,float DegStep)
{
	if ( From > To )
		DegStep = -fabsf( DegStep );
	
	for ( float d=From;	d<To;	d+=DegStep )
	{
		AngleDegs.PushBack( d );
	}
}

#define PI	kPiF
#define kPiF			(3.14159265358979323846264338327950288f)
#define kDegToRad		(kPiF / 180.0f)
#define kRadToDeg		(180.0f / kPiF)

//	LatLonToView
vec3f LatLonToView(vec2f latlon)
{
	//	http://en.wikipedia.org/wiki/N-vector#Converting_latitude.2Flongitude_to_n-vector
	auto latitude = latlon.x;
	auto longitude = latlon.y;
	auto las = sin(latitude);
	auto lac = cos(latitude);
	auto los = sin(longitude);
	auto loc = cos(longitude);
	
	vec3f View( los * lac, las, loc * lac );
	//assert(fabsf(result.Length() - 1.0f) < 0.01f);

	return View;
}

vec2f ViewToLatLon(vec3f View3)
{
	//	http://en.wikipedia.org/wiki/N-vector#Converting_n-vector_to_latitude.2Flongitude
	auto x = View3.x;
	auto y = View3.y;
	auto z = View3.z;
	
	/*
	 $lat = atan( $z / sqrt( ($x*$x) + ($y*$y) ) );
	 //$lat = asin( $z );
	 $lon = 0;
	 if ( $x != 0 )
	 $lon = atan( $y / $x );
	 */
	
	auto lat = atan2( x, z );
	
	//	normalise y
	auto xz = sqrt( (x*x) + (z*z) );
	auto normy = y / sqrt( (y*y) + (xz*xz) );
	auto lon = asin( normy );
	//$lon = atan2( $y, $xz );
	
	//	stretch longitude...
	lon *= 2.0;
	
	return vec2f( lat, lon );
}

//	-PI...PI, -PI...PI
vec2f ScreenEquirectToLatLon(vec2f ScreenPos,float Width,float Height)
{
	auto xmul = 2.0;
	auto xsub = 1.0;
	auto ymul = 2.0;
	auto ysub = 1.0;
	
	auto xfract = ScreenPos.x / Width;
	xfract *= xmul;
	
	auto yfract = (Height - ScreenPos.y) / Height;
	yfract *= ymul;
	
	float lat = ( xfract - xsub) * kPiF;
	float lon = ( yfract - ysub) * kPiF;
	return vec2f( lat, lon );
}


vec2f LatLonToScreen(vec2f LatLon,float Width,float Height)
{
	//	-pi...pi -> -1...1
	auto& lat = LatLon.x;
	auto& lon = LatLon.y;
	lat /= kPiF;
	lon /= kPiF;
	
	//	-1..1 -> 0..2
	lat += 1.0;
	lon += 1.0;
	
	//	0..2 -> 0..1
	lat /= 2.0;
	lon /= 2.0;
	
	lon = 1.0 - lon;
	lat *= Width;
	lon *= Height;
	
	return vec2f( lat, lon );
}


/*



vec2f ScreenToLatLong(float x, float y, float Width, float Height)
{
	static float xmul = 2.f;
	static float xsub = 1.0f;
	float xfract = x / Width;
	xfract *= xmul;
	
	static float ysub = 0.5f;
	static float ymul = 1.f;
	float yfract = (Height - y) / Height;
	yfract *= ymul;
	
	float lon = ( xfract - xsub) * kPiF;
	float lat = ( yfract - ysub) * kPiF;
	static float lonmin = lon;
	static float latmin = lat;
	static float lonmax = lon;
	static float latmax = lat;
	lonmin = std::min( lon, lonmin );
	lonmax = std::max( lon, lonmax );
	latmin = std::min( lat, latmin );
	latmax = std::max( lat, latmax );
	return vec2f( lat, lon );
}


vec3f LatLongToView(vec2f latlon)
{
	float latitude = latlon.x;
	float longitude = latlon.y;
	float las = sinf(latitude);
	float lac = cosf(latitude);
	float los = sinf(longitude);
	float loc = cosf(longitude);
	
	vec3f result( los * lac, las, loc * lac );
	assert(fabsf(result.Length() - 1.0f) < 0.01f);
	
	return result;
}

vec2f LatLongToScreen(vec2f LatLon)
{
	//	gr: get latest from panopoly...
	
	// The largest coordinate component determines which face we’re looking at.
	//	coords = pixel (camera) normal
	vec3f coords = VectorFromCoordsRad(Coords);
	float ax = fabsf(coords.x);
	float ay = fabsf(coords.y);
	float az = fabsf(coords.z);
	vec2f faceOffset;
	float x, y;	//	offset from center of face
	
	assert(ax != 0.0f || ay != 0.0f || az != 0.0f);
	
	if (ax > ay && ax > az)
	{//	facing x
		x = coords.z / ax;
		y = -coords.y / ax;
		if (0 < coords.x)
		{
			x = -x;
			faceOffset = pxPos;
		}
		else
		{
			faceOffset = nxPos;
		}
	}
	else if (ay > ax && ay > az)
	{//	facing y
		x = coords.x / ay;
		y = coords.z / ay;
		if (0 < coords.y)
		{
			faceOffset = pyPos;
		}
		else
		{
			y = -y;
			faceOffset = nyPos;
		}
	}
	else
	{//	must be facing z
		x = coords.x / az;
		y = -coords.y / az;
		if (0 < coords.z)
		{
			faceOffset = pzPos;
		}
		else
		{
			x = -x;
			faceOffset = nzPos;
		}
	}
	
	faceOffset.x *= mFaceSize.x;
	faceOffset.y *= mFaceSize.y;
	x = (x * mHalfSize.x) + mHalfSize.x;
	y = (y * mHalfSize.y) + mHalfSize.y;
	assert( x >= 0 && x <= mFaceSize.x );
	assert( y >= 0 && y <= mFaceSize.y );
	
	
	auto result = FPMSampleLinear( Pixels, x + faceOffset.x, y + faceOffset.y, TWrapMode::Clamp, TWrapMode::Clamp );
	return result;
}

vec2f ViewToScreen(const SoyPixels& Image,vec3f View)
{

}

vec3f EularToView(float Pitch,float Yaw,float Roll)
{
	
}
 */

template<typename TESTFUNC>
bool FindFirstMatch(vec2f& MatchLatLon,ArrayBridge<float>&& Lats,ArrayBridge<float>&& Lons,TESTFUNC TestFunc)
{
	for ( int pi=0;	pi<Lats.GetSize();	pi++ )
	{
		for ( int yi=0;	yi<Lons.GetSize();	yi++ )
		{
			vec2f LatLon( Lats[pi], Lons[yi] );

			if ( !TestFunc( LatLon ) )
				continue;
			
			MatchLatLon = LatLon;
			return true;
		}
	}
	return false;
}

vec2f FindHolePos(const SoyPixels& Image,std::stringstream& Error)
{
	//	search 3D space until we find a black pixel
	
	Array<float> Lats;
	Array<float> Lons;
	static float LatMin = -PI;
	static float LatMax = PI;
	static float LonMin = -PI;
	static float LonMax = PI;
	static float LatStep = 0.01f;
	static float LonStep = 0.01f;
	MakeAngles( GetArrayBridge(Lats), LatMin, LatMax, LatStep );
	MakeAngles( GetArrayBridge(Lons), LonMin, LonMax, LonStep );

	auto IsBlack = [Image](vec2f LatLon)
	{
		auto Screen2 = LatLonToScreen( LatLon, Image.GetWidth(), Image.GetHeight() );
		Screen2.x = std::min<float>( Image.GetWidth()-1, std::max<float>( 0, Screen2.x ) );
		Screen2.y = std::min<float>( Image.GetHeight()-1, std::max<float>( 0, Screen2.y ) );
		auto r = Image.GetPixel( Screen2.x, Screen2.y, 0 );
		auto g = Image.GetPixel( Screen2.x, Screen2.y, 1 );
		auto b = Image.GetPixel( Screen2.x, Screen2.y, 2 );
		//static int Tolerance = 10+5+5;	//	gr: rugby "black spot" isn't black at all!
		static int Tolerance = 2;	//	gr: rugby "black spot" isn't black at all!
		static int Min = 256+256+256;
		Min = std::min( Min, r+g+b);
		return (r+g+b) <= Tolerance;
	};
	
	//	find first black spot
	vec2f FirstBlackLatLon;
	if ( !FindFirstMatch( FirstBlackLatLon, GetArrayBridge(Lats), GetArrayBridge(Lons), IsBlack ) )
	{
		Error << "Failed to find black pixel";
		return vec2f(0,0);
	}
	
	return LatLonToScreen( FirstBlackLatLon, Image.GetWidth(), Image.GetHeight() );
	
	//	nudge up in 3d until we find top3
	//	nudge down until we find bottom3
	//	get midvert3
	//	cross
	//	nudge left until we find left3
	//	nudge right until we find right3
	//	get midhorz3
	
	//	cross and find midvert3 again?
	
	//	convert to 2D and report
	return vec2f( Image.GetWidth()/2, Image.GetHeight()/2 );
}


TPopDistort::TPopDistort()
{
	TParameterTraits FindHoleTraits;
	FindHoleTraits.mRequiredKeys.PushBack("image");
	AddJobHandler("findhole", FindHoleTraits, *this, &TPopDistort::OnFindHole );
}

void TPopDistort::AddChannel(std::shared_ptr<TChannel> Channel)
{
	TChannelManager::AddChannel( Channel );
	if ( !Channel )
		return;
	TJobHandler::BindToChannel( *Channel );
}


void TPopDistort::OnFindHole(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	SoyPixels Image;
	if ( !Job.mParams.GetParamAs("image",Image) )
	{
		std::stringstream Error;
		Error << "Failed to decode image param";
		TJobReply Reply( JobAndChannel );
		Reply.mParams.AddErrorParam( Error.str() );
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}
	
	std::stringstream Error;

	//	find position
	vec2f HolePos2 = FindHolePos( Image, Error );
	
	TJobReply Reply( JobAndChannel );
	
	//	non-registered type, so pre-encode
	std::shared_ptr<SoyData_Stack<std::string>> HolePos2String( new SoyData_Stack<std::string>() );
	HolePos2String->Encode( SoyData_Impl<vec2f>(HolePos2) );
	Reply.mParams.AddParam("pos2", HolePos2String );
	
	if ( !Error.str().empty() )
		Reply.mParams.AddErrorParam( Error.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}



//	horrible global for lambda
std::shared_ptr<TChannel> gStdioChannel;
std::shared_ptr<TChannel> gCaptureChannel;



TPopAppError::Type PopMain(TJobParams& Params)
{
	std::cout << Params << std::endl;
	
	TPopDistort App;

	auto CommandLineChannel = std::shared_ptr<TChan<TChannelLiteral,TProtocolCli>>( new TChan<TChannelLiteral,TProtocolCli>( SoyRef("cmdline") ) );
	
	//	create stdio channel for commandline output
	gStdioChannel = CreateChannelFromInputString("std:", SoyRef("stdio") );
	auto HttpChannel = CreateChannelFromInputString("http:8080-8090", SoyRef("http") );
	auto WebSocketChannel = CreateChannelFromInputString("ws:json:9090-9099", SoyRef("websock") );
//	auto WebSocketChannel = CreateChannelFromInputString("ws:cli:9090-9099", SoyRef("websock") );
	auto SocksChannel = CreateChannelFromInputString("cli:7090-7099", SoyRef("socks") );
	
	
	App.AddChannel( CommandLineChannel );
	App.AddChannel( gStdioChannel );
	App.AddChannel( HttpChannel );
	App.AddChannel( WebSocketChannel );
	App.AddChannel( SocksChannel );

	//	when the commandline SENDs a command (a reply), send it to stdout
	auto RelayFunc = [](TJobAndChannel& JobAndChannel)
	{
		if ( !gStdioChannel )
			return;
		TJob Job = JobAndChannel;
		Job.mChannelMeta.mChannelRef = gStdioChannel->GetChannelRef();
		Job.mChannelMeta.mClientRef = SoyRef();
		gStdioChannel->SendCommand( Job );
	};
	CommandLineChannel->mOnJobSent.AddListener( RelayFunc );
	
	//	connect to another app, and subscribe to frames
	static bool CreateCaptureChannel = false;
	static bool SubscribeToCapture = false;
	static bool SendStdioToCapture = false;
	if ( CreateCaptureChannel )
	{
		auto CaptureChannel = CreateChannelFromInputString("cli://localhost:7070", SoyRef("capture") );
		gCaptureChannel = CaptureChannel;
		CaptureChannel->mOnJobRecieved.AddListener( RelayFunc );
		App.AddChannel( CaptureChannel );
		
		//	send commands from stdio to new channel
		if ( SendStdioToCapture )
		{
			auto SendToCaptureFunc = [](TJobAndChannel& JobAndChannel)
			{
				TJob Job = JobAndChannel;
				Job.mChannelMeta.mChannelRef = gStdioChannel->GetChannelRef();
				Job.mChannelMeta.mClientRef = SoyRef();
				gCaptureChannel->SendCommand( Job );
			};
			gStdioChannel->mOnJobRecieved.AddListener( SendToCaptureFunc );
		}
		
		if ( SubscribeToCapture )
		{
			auto StartSubscription = [](TChannel& Channel)
			{
				TJob GetFrameJob;
				GetFrameJob.mChannelMeta.mChannelRef = Channel.GetChannelRef();
				//GetFrameJob.mParams.mCommand = "subscribenewframe";
				//GetFrameJob.mParams.AddParam("serial", "isight" );
				GetFrameJob.mParams.mCommand = "getframe";
				GetFrameJob.mParams.AddParam("serial", "isight" );
				GetFrameJob.mParams.AddParam("memfile", "1" );
				Channel.SendCommand( GetFrameJob );
			};
	
			CaptureChannel->mOnConnected.AddListener( StartSubscription );
		}
	}
	
	
	{
		auto& Channel = *CommandLineChannel;
		TJob Job;
		Job.mChannelMeta.mChannelRef = Channel.GetChannelRef();
		Job.mParams.mCommand = "decode";
		std::string Filename = "/Users/grahamr/Desktop/mapdump/data1.txt";
		Job.mParams.AddDefaultParam( Filename, "text/file/binary" );
		Channel.OnJobRecieved(Job);
	}
	
	//	run
	App.mConsoleApp.WaitForExit();

	gStdioChannel.reset();
	return TPopAppError::Success;
}
