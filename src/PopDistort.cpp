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

vec2f FindHolePos(const SoyPixels& Image,std::stringstream& Error)
{
	//	search 3D space until we find a black pixel
	
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
