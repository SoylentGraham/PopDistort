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



TPopFeatures::TPopFeatures()
{
	AddJobHandler("exit", TParameterTraits(), *this, &TPopFeatures::OnExit );
	
	AddJobHandler("newframe", TParameterTraits(), *this, &TPopFeatures::OnNewFrame );
	AddJobHandler("re:getframe", TParameterTraits(), *this, &TPopFeatures::OnNewFrame );
	
	TParameterTraits GetFeatureTraits;
	GetFeatureTraits.mAssumedKeys.PushBack("x");
	GetFeatureTraits.mAssumedKeys.PushBack("y");
	GetFeatureTraits.mRequiredKeys.PushBack("image");
	AddJobHandler("getfeature", GetFeatureTraits, *this, &TPopFeatures::OnGetFeature );

	TParameterTraits FindFeatureTraits;
	FindFeatureTraits.mAssumedKeys.PushBack("feature");
	FindFeatureTraits.mRequiredKeys.PushBack("image");
	AddJobHandler("findfeature", FindFeatureTraits, *this, &TPopFeatures::OnFindFeature );
	
	TParameterTraits TrackFeaturesTraits;
	TrackFeaturesTraits.mAssumedKeys.PushBack("sourcefeatures");
	//TrackFeaturesTraits.mRequiredKeys.PushBack("image");
	AddJobHandler("trackfeatures", TrackFeaturesTraits, *this, &TPopFeatures::OnTrackFeatures );
	
	TParameterTraits FindInterestingFeaturesTraits;
	//FindInterestingFeaturesTraits.mRequiredKeys.PushBack("image");
	AddJobHandler("findinterestingfeatures", FindInterestingFeaturesTraits, *this, &TPopFeatures::OnFindInterestingFeatures );
	
	TParameterTraits SendPingTraits;
	SendPingTraits.mAssumedKeys.PushBack("channel");
	AddJobHandler("sendping", SendPingTraits, *this, &TPopFeatures::OnSendPing );

	AddJobHandler("re:ping", TParameterTraits(), *this, &TPopFeatures::OnRePing );

	
	AddJobHandler("decode", TParameterTraits(), *this, &TPopFeatures::OnDecode );
}

void TPopFeatures::AddChannel(std::shared_ptr<TChannel> Channel)
{
	TChannelManager::AddChannel( Channel );
	if ( !Channel )
		return;
	TJobHandler::BindToChannel( *Channel );
}


void TPopFeatures::OnExit(TJobAndChannel& JobAndChannel)
{
	mConsoleApp.Exit();
	
	//	should probably still send a reply
	TJobReply Reply( JobAndChannel );
	Reply.mParams.AddDefaultParam(std::string("exiting..."));
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopFeatures::OnGetFeature(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	//	pull image
	auto ImageParam = Job.mParams.GetParam("image");

	/*
	//	assume format is now set right, decode the pixels out of it
	SoyPixels Pixels;
	if ( !ImageParam.Decode(Pixels) )
	{
		std::stringstream Error;
		Error << "Failed to decode " << Job.mParams.GetParamAs<std::string>("image") << " to an image";
		TJobReply Reply( JobAndChannel );
		Reply.mParams.AddErrorParam( Error.str() );
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}
	*/
	
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
	
	auto PixelxParam = Job.mParams.GetParam("x");
	auto PixelyParam = Job.mParams.GetParam("y");
	int x = PixelxParam.Decode<int>();
	int y = PixelyParam.Decode<int>();
	


	//	return descriptor and stuff
	std::stringstream Error;
	TFeatureBinRingParams Params( Job.mParams );
	TFeatureBinRing Feature;
	TFeatureExtractor::GetFeature( Feature, Image, x, y, Params, Error );
	
	TJobReply Reply( JobAndChannel );
	
	SoyData_Impl<TFeatureBinRing> FeatureData( Feature );
	std::shared_ptr<SoyData> FeatureEncoded( new SoyData_Stack<std::string>() );
	static_cast<SoyData_Stack<std::string>&>(*FeatureEncoded).Encode( FeatureData );
	
	Reply.mParams.AddDefaultParam( FeatureEncoded );
	Reply.mParams.AddParam( PixelxParam );
	Reply.mParams.AddParam( PixelyParam );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopFeatures::OnSendPing(TJobAndChannel &JobAndChannel)
{
	const TJob& Job = JobAndChannel;
	
	//	find the target channel to send to
	auto PingChannelString = Job.mParams.GetParamAs<std::string>("channel");
	SoyRef PingChannelRef( PingChannelString.c_str() );

	//	nothing or * sends to all
	if ( PingChannelString == "*" )
		PingChannelRef = SoyRef();
	auto PingChannel = GetChannel( PingChannelRef );
	
	if ( !PingChannel && PingChannelRef != SoyRef() )
	{
		//	get a list of channels
		std::stringstream Error;
		Error << "Channel " << PingChannelRef << " not found; try... ";
		auto& Channels = mChannels;
		for ( auto it=Channels.begin();	it!=Channels.end();	it++ )
		{
			auto& Channel = **it;
			Error << Channel.GetChannelRef() << " ";
		}
		
		TJobReply Reply( JobAndChannel );
		Reply.mParams.AddErrorParam( Error.str() );
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}
	
	//	send new command via specified channel
	TJob PingCommand;
	PingCommand.mParams.mCommand = "ping";
	
	//	lets add something big!
	Array<char> Dummy(900000);
	for ( int i=0;	i<Dummy.GetSize();	i++ )
	{
		Dummy[i] = i % 256;
	}
	PingCommand.mParams.AddDefaultParam( Dummy );
	
	//	make a ping by sending the current timestamp then we compare it on return
	SoyTime Now(true);
	PingCommand.mParams.AddParam("sendtime", Now );
	
	//	if no ping channel, send to all (good test)
	Array<SoyRef> SentToChannels;
	if ( PingChannel )
	{
		PingCommand.mChannelMeta.mChannelRef = PingChannel->GetChannelRef();
		if ( PingChannel->SendCommand( PingCommand ) )
			SentToChannels.PushBack( PingChannel->GetChannelRef() );
	}
	else
	{
		auto& Channels = mChannels;
		for ( auto it=Channels.begin();	it!=Channels.end();	it++ )
		{
			auto& Channel = **it;
			PingCommand.mChannelMeta.mChannelRef = Channel.GetChannelRef();
			if ( Channel.SendCommand( PingCommand ) )
				SentToChannels.PushBack( Channel.GetChannelRef() );
		}
	}
	
	//	gratuity reply (for http etc)
	TJobReply Reply( JobAndChannel );
	std::stringstream ReplyString;
	ReplyString << "send ping to " << SentToChannels.GetSize() << " channels; ";
	for ( int i=0;	i<SentToChannels.GetSize();	i++ )
		ReplyString << SentToChannels[i] << " ";
	Reply.mParams.AddDefaultParam( ReplyString.str() );
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );

}

void TPopFeatures::OnRePing(TJobAndChannel &JobAndChannel)
{
	const TJob& Job = JobAndChannel;
	
	//	read the send time
	auto SendTime = Job.mParams.GetParamAs<SoyTime>("sendtime");
	SoyTime Now(true);
	
	if ( !SendTime.IsValid() )
	{
		std::Debug << Job.mParams.mCommand << " has invalid send time, cannot calculate ping :(" << std::endl;
		return;
	}
	
	//	compare against now
	if ( SendTime.GetTime() > Now.GetTime() )
	{
		std::Debug << Job.mParams.mCommand << " sendtime(" << SendTime.GetTime() << ") is in future (now: " << Now.GetTime() << ")" << std::endl;
		return;
	}
	
	auto TimeDiff = Now.GetTime() - SendTime.GetTime();
	
	//	gr: because sometimes the reply is so quick, this gets mangled with other std::debug (locking stillisnt working!)
	//		so for now lets sleep so we cna read the ping
	std::this_thread::sleep_for( std::chrono::milliseconds(1000) );
	std::Debug << Job.mParams.mCommand << " ping is " << TimeDiff << "ms" << std::endl;
}


void ScoreInterestingFeatures(ArrayBridge<TFeatureMatch>&& Features,float MinScore)
{
	std::map<std::string,int> FeatureHistogram;		//	build a histogram to work out how unique the features are
	int HistogramMaxima = 0;

	for ( int f=0;	f<Features.GetSize();	f++ )
	{
		auto& Feature = Features[f];
		std::stringstream FeatureString;
		FeatureString << Feature.mFeature;
		auto& FeatureCount = FeatureHistogram[FeatureString.str()];
		FeatureCount++;
		HistogramMaxima = std::max( HistogramMaxima, FeatureCount );
	}
	
	//	now re-apply the feature's score based on their uniqueness in the histogram
	for ( int f=Features.GetSize()-1;	f>=0;	f-- )
	{
		auto& Feature = Features[f];
		auto& Score = Feature.mScore;
		std::stringstream FeatureString;
		FeatureString << Feature.mFeature;
		auto Occurrance = FeatureHistogram[FeatureString.str()];
		Score = 1.f - (Occurrance / static_cast<float>(HistogramMaxima));
		
		//	cull if score is too low
		if ( Score >= MinScore )
			continue;
		
		Features.RemoveBlock(f,1);
	}
}

void TPopFeatures::OnFindInterestingFeatures(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	//	decode image now into a param so we can send back the one we used
	std::shared_ptr<SoyData_Stack<SoyPixels>> ImageData( new SoyData_Stack<SoyPixels>() );
	auto& Image = ImageData->mValue;
	auto ImageParam = Job.mParams.GetParam( TJobParam::Param_Default );
	if ( !ImageParam.Decode( *ImageData ) )
	{
		std::stringstream Error;
		Error << "Failed to decode image param (" << Image.GetFormat() << ")";
		TJobReply Reply( JobAndChannel );
		Reply.mParams.AddErrorParam( Error.str() );
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}
	
	//	resize for speed
	//	gr: put this in params!
//	static int NewWidth = 400;
//	static int NewHeight = 200;
//	Image.ResizeFastSample(NewWidth,NewHeight);

	//	grab a feature at each point on a grid on the image
	TFeatureBinRingParams Params( Job.mParams );
	Array<TFeatureMatch> FeatureMatches;
	FeatureMatches.Reserve( (Image.GetHeight()/Params.mMatchStepY) * (Image.GetWidth()/Params.mMatchStepX) );
	std::stringstream Error;
	for ( int y=0;	y<Image.GetHeight();	y+=Params.mMatchStepY )
	{
		for ( int x=0;	x<Image.GetWidth();	x+=Params.mMatchStepX )
		{
			TFeatureBinRing Feature;
			TFeatureExtractor::GetFeature( Feature, Image, x, y, Params, Error );
			if ( !Error.str().empty() )
				break;
			auto& Match = FeatureMatches.PushBack();
			Match.mSourceCoord = vec2x<int>(-1,-1);
			Match.mCoord.x = x;
			Match.mCoord.y = y;
			Match.mFeature = Feature;
			Match.mScore = 0.f;	//	make interesting score
		}
	}
	
	//	do initial scoring to remove low-interest features
	ScoreInterestingFeatures( GetArrayBridge( FeatureMatches ), Params.mMinInterestingScore );

	//	re-score to normalise the score. (could probably do this faster, but this is simpler)
	ScoreInterestingFeatures( GetArrayBridge( FeatureMatches ), 0.f );
	
	//	some some params back with the reply
	TJobReply Reply( JobAndChannel );
	
	//	gr: repalce with desired format/container
	bool AsJson = Job.mParams.GetParamAsWithDefault("asjson", false );
	bool AsBinary = Job.mParams.GetParamAsWithDefault("asbinary", false );
	
	if ( AsJson )
	{
		//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
		std::shared_ptr<SoyData_Impl<json::Object>> FeatureMatchesJsonData( new SoyData_Stack<json::Object>() );
		if ( FeatureMatchesJsonData->EncodeRaw( FeatureMatches ) )
		{
			std::shared_ptr<SoyData> FeatureMatchesJsonDataGen( FeatureMatchesJsonData );
			Reply.mParams.AddDefaultParam( FeatureMatchesJsonDataGen );
		}
	}
	
	
	if ( AsBinary )
	{
		static bool JoinFeaturesAndImage = true;
		
		if ( JoinFeaturesAndImage )
		{
			TFeatureMatchesAndImage MaI;
			SoyData_Impl<TFeatureMatchesAndImage> MaIData( MaI );
			MaI.mFeatureMatches = FeatureMatches;
			MaI.mImage = Image;
			
			//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
			std::shared_ptr<SoyData_Impl<Array<char>>> MaiBinary( new SoyData_Stack<Array<char>>() );
			if ( MaiBinary->EncodeRaw( MaI ) )
			{
				Reply.mParams.AddDefaultParam( MaiBinary );
			}
		}
		else
		{
			//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
			std::shared_ptr<SoyData_Impl<Array<char>>> FeatureMatchesJsonData( new SoyData_Stack<Array<char>>() );
			if ( FeatureMatchesJsonData->EncodeRaw( FeatureMatches ) )
			{
				Reply.mParams.AddDefaultParam( FeatureMatchesJsonData );
			}
		}
	}
	
	//	add as generic
	if ( !Reply.mParams.HasDefaultParam() )
		Reply.mParams.AddDefaultParam( FeatureMatches );
	
	if ( !Error.str().empty() )
		Reply.mParams.AddErrorParam( Error.str() );
	
	//	gr: need to work out a good way to automatically send back token/meta params (all the ones we didn't read?)
	auto SerialParam = Job.mParams.GetParam("serial");
	Reply.mParams.AddParam( SerialParam );

	//	gr: this sends a big payload... and a MASSIVE image as a string param!, but maybe need it at some point
	static bool SendBackImageImage = false;
	if ( SendBackImageImage )
	{
		//	gr: send back a decoded image, not the original param (which might be a now-different memfile)
		Reply.mParams.AddParam("image",ImageData);
	}
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}

void TPopFeatures::OnFindFeature(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	//	pull image
	auto Image = Job.mParams.GetParamAs<SoyPixels>("image");
	auto Feature = Job.mParams.GetParamAs<TFeatureBinRing>("Feature");
	
	if ( !Image.IsValid() )
	{
		std::stringstream Error;
		Error << "Failed to decode image param";
		TJobReply Reply( JobAndChannel );
		Reply.mParams.AddErrorParam( Error.str() );
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}

	//	run a search
	TFeatureBinRingParams Params( Job.mParams );
	Array<TFeatureMatch> FeatureMatches;
	std::stringstream Error;
	TFeatureExtractor::FindFeatureMatches( GetArrayBridge(FeatureMatches), Image, Feature, Params, Error );
	
	//	some some params back with the reply
	TJobReply Reply( JobAndChannel );
	Reply.mParams.AddParam( Job.mParams.GetParam("Feature") );
	
	
	//	gr: repalce with desired format/container
	bool AsJson = Job.mParams.GetParamAsWithDefault("asjson", false );
	bool AsBinary = Job.mParams.GetParamAsWithDefault("asbinary", false );
	
	if ( AsJson )
	{
		//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
		std::shared_ptr<SoyData_Impl<json::Object>> FeatureMatchesJsonData( new SoyData_Stack<json::Object>() );
		if ( FeatureMatchesJsonData->EncodeRaw( FeatureMatches ) )
		{
			std::shared_ptr<SoyData> FeatureMatchesJsonDataGen( FeatureMatchesJsonData );
			Reply.mParams.AddDefaultParam( FeatureMatchesJsonDataGen );
		}
	}
	
	if ( AsBinary )
	{
		//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
		std::shared_ptr<SoyData_Impl<Array<char>>> FeatureMatchesJsonData( new SoyData_Stack<Array<char>>() );
		if ( FeatureMatchesJsonData->EncodeRaw( FeatureMatches ) )
		{
			std::shared_ptr<SoyData> FeatureMatchesJsonDataGen( FeatureMatchesJsonData );
			Reply.mParams.AddDefaultParam( FeatureMatchesJsonDataGen );
		}
	}
	
	//	add as generic
	if ( !Reply.mParams.HasDefaultParam() )
		Reply.mParams.AddDefaultParam( FeatureMatches );
	
	if ( !Error.str().empty() )
		Reply.mParams.AddErrorParam( Error.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopFeatures::OnTrackFeatures(TJobAndChannel& JobAndChannel)
{
	//	just grab interesting ones for now
	OnFindInterestingFeatures( JobAndChannel );
/*
	auto& Job = JobAndChannel.GetJob();
	
	//	pull image
	auto Image = Job.mParams.GetParamAs<SoyPixels>("image");
	auto SourceFeatures = Job.mParams.GetParamAs<Array<TFeatureMatch>>("sourcefeatures");
	
	if ( !Image.IsValid() )
	{
		std::stringstream Error;
		Error << "Failed to decode image param";
		TJobReply Reply( JobAndChannel );
		Reply.mParams.AddErrorParam( Error.str() );
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}
	
	//	find nearest neighbour best match...
	
	//	run a search
	TFeatureBinRingParams Params( Job.mParams );
	Array<TFeatureMatch> FeatureMatches;
	std::stringstream Error;
	//TFeatureExtractor::FindFeatureMatches( GetArrayBridge(FeatureMatches), Image, Feature, Params, Error );
	for ( int f=0;	f<SourceFeatures.GetSize();	f++ )
	{
		auto& SourceFeature = SourceFeatures[f];
		auto& Match = FeatureMatches.PushBack();
		Match.mSourceCoord = SourceFeature.mCoord;
		Match.mSourceFeature = SourceFeature.mFeature;
		
		Match.mCoord = SourceFeature.mCoord;
		Match.mCoord.x += 10;
		Match.mCoord.y += 10;
		Match.mFeature = SourceFeature.mFeature;
	}

	//	some some params back with the reply
	TJobReply Reply( JobAndChannel );
	Reply.mParams.AddParam( Job.mParams.GetParam("serial") );
	
	
	//	gr: repalce with desired format/container
	bool AsJson = Job.mParams.GetParamAsWithDefault("asjson", false );
	bool AsBinary = Job.mParams.GetParamAsWithDefault("asbinary", false );
	
	if ( AsJson )
	{
		//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
		std::shared_ptr<SoyData_Impl<json::Object>> FeatureMatchesJsonData( new SoyData_Stack<json::Object>() );
		if ( FeatureMatchesJsonData->EncodeRaw( FeatureMatches ) )
		{
			std::shared_ptr<SoyData> FeatureMatchesJsonDataGen( FeatureMatchesJsonData );
			Reply.mParams.AddDefaultParam( FeatureMatchesJsonDataGen );
		}
	}
	
	if ( AsBinary )
	{
		//	gr: the internal SoyData system doesn't know this type, so won't auto encode :/ need to work on this!
		std::shared_ptr<SoyData_Impl<Array<char>>> FeatureMatchesJsonData( new SoyData_Stack<Array<char>>() );
		if ( FeatureMatchesJsonData->EncodeRaw( FeatureMatches ) )
		{
			std::shared_ptr<SoyData> FeatureMatchesJsonDataGen( FeatureMatchesJsonData );
			Reply.mParams.AddDefaultParam( FeatureMatchesJsonDataGen );
		}
	}
	
	//	add as generic
	if ( !Reply.mParams.HasDefaultParam() )
		Reply.mParams.AddDefaultParam( FeatureMatches );
	
	if ( !Error.str().empty() )
		Reply.mParams.AddErrorParam( Error.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
 */
}



void TPopFeatures::OnNewFrame(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	//	pull image
	auto ImageParam = Job.mParams.GetDefaultParam();
	SoyPixels Image;
	std::Debug << "Getting image from " << ImageParam.GetFormat() << std::endl;
	if ( !ImageParam.Decode( Image ) )
	{
		std::Debug << "Failed to decode image" << std::endl;
		return;
	}
	std::Debug << "Decoded image " << Image.GetWidth() << "x" << Image.GetHeight() << " " << Image.GetFormat() << std::endl;
}



//	horrible global for lambda
std::shared_ptr<TChannel> gStdioChannel;
std::shared_ptr<TChannel> gCaptureChannel;



TPopAppError::Type PopMain(TJobParams& Params)
{
	std::cout << Params << std::endl;
	
	TPopFeatures App;

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


class TProtoBuf
{
public:
	TProtoBuf(ArrayBridge<char>&& Data)
	{
		Decode(Data);
	}
	
	bool		Decode(ArrayBridge<char>& Data);
	
	int			GetVarInt(char* Data,int Bytes);
	
public:
};


#include <bitset>

namespace TProtoBufFieldType
{
	enum Type
	{
		Varint	= 0,		//	Varint	int32, int64, uint32, uint64, sint32, sint64, bool, enum
		SixtyFourBit = 1,	//	64-bit	fixed64, sfixed64, double
		LengthDelim = 2,	//	Length-delimited	string, bytes, embedded messages, packed repeated fields
		StartGroup = 3,		//	Start group	groups (deprecated)
		EndGroup = 4,		//	End group	groups (deprecated)
		ThirtyTwoBit = 5,	//	32-bit	fixed32, sfixed32, float
	};
	
};


bool EatVarInt(uint64& Varint,TBitReader& BitReader)
{
	
	BufferArray<uint8,10> IntParts;
	int MoreData = 0;
	do
	{
		//	eat MSB bit
		if ( !BitReader.Read( MoreData, 1 ) )
			return false;
		
		uint8 IntPart;
		if ( !BitReader.Read( IntPart, 7 ) )
			return false;
		IntParts.PushBack(IntPart);
	}
	while ( MoreData );

	//	If you use int32 or int64 as the type for a negative number, the resulting varint is always ten bytes long
	//	– it is, effectively, treated like a very large unsigned integer.
	if ( IntParts.GetSize() == 10 )
	{
		std::Debug << "handle negative int32/int64" << std::endl;
		return false;
	}

	//	zig zag encoding
	// If you use one of the signed types, the resulting varint uses ZigZag encoding, which is much more efficient.

		 
	BufferArray<char,100> IntContents;
	Varint = 0;
	for ( int p=0;	p<IntParts.GetSize();	p++ )
		Varint |= IntParts[p] << (7*p);

	int BitCount = IntParts.GetSize() * 7;
	
	if ( !Soy::Assert( BitCount <= 64, std::stringstream()<<"Varint is " << BitCount << "bits (" << Varint << ") . we max at 32" ) )
		return false;

	return true;
}


uint64 TestEat2()
{
	BufferArray<char,100> ThreeHundred;
	ThreeHundred.PushBack(0xAC);
	ThreeHundred.PushBack(0x02);
	
	TBitReader BitReader( GetArrayBridge(ThreeHundred) );
	
	uint64 KeyType;
	if ( !EatVarInt( KeyType, BitReader ) )
		return false;
	
	std::Debug << KeyType << " = 300 ? " << std::endl;
	
	return KeyType;
}


uint64 TestEat()
{
	BufferArray<char,100> One;
	One.PushBack( 0x00000001 );
	
	TBitReader BitReader( GetArrayBridge(One) );

	uint64 KeyType;
	if ( !EatVarInt( KeyType, BitReader ) )
		return false;

	std::Debug << KeyType << " = 1 ? " << std::endl;

	return KeyType;
}

TJobParam ReadParamData(TBitReader& BitReader,TProtoBufFieldType::Type Type)
{
	switch ( Type )
	{
		case TProtoBufFieldType::Varint:
		{
			uint64 Value;
			if ( !EatVarInt( Value, BitReader ) )
				return TJobParam();
			
			int ValueInt = Value;
			TJobParam Param;
			Param.mSoyData.reset( new SoyData_Stack<int>(ValueInt) );
			return Param;
		}
		break;
			


		default:
			std::Debug << "Unhandled type: " << Type << std::endl;
			return TJobParam();
	}
}

TJobParam ReadNextElement(TBitReader& BitReader)
{
	//	read key
	uint64 Key;
	if ( !EatVarInt( Key, BitReader ) )
		return TJobParam();
		
	//	type is first 3 bits
	auto Type = static_cast<TProtoBufFieldType::Type>( Key & (0x7) );
	uint64 Field = Key >> 3;
	
	//	fields start at 1
	if ( Field == 0 )
		return TJobParam();

	TJobParam Param = ReadParamData( BitReader, Type );
	Param.mName = (std::stringstream() << Field).str();

	return Param;
}


//	https://developers.google.com/protocol-buffers/docs/encoding
bool TProtoBuf::Decode(ArrayBridge<char>& Data)
{
	TestEat();
	TestEat2();
	
	static bool DoSimpleExample = false;
	if ( DoSimpleExample )
	{
		//	simple example is 150 int32
		Data.Clear();
		Data.PushBack(0x8);
		Data.PushBack(0x96);
		Data.PushBack(0x1);
	}
	std::Debug << "proto buf " << Data.GetDataSize() << " bytes" << std::endl;
	
	

	TBitReader BitReader(Data);
	TJobParams Params;
	
	while ( !BitReader.Eof() )
	{
		TJobParam Param = ReadNextElement( BitReader );
		if ( !Param.IsValid() )
			return false;
		Params.AddParam( Param );
	}
	std::Debug << "read protobuf params: " << Params << std::endl;

	/*
	//	read the EOF msb
	int Eof;
	if ( !BitReader.Read( Eof, 1 ) )
		return false;
	//	rea
	*/
	
	//	read key index
	
	return false;
}



void TPopFeatures::OnDecode(TJobAndChannel& JobAndChannel)
{
	auto DataParam = JobAndChannel.GetJob().mParams.GetDefaultParam();
	TJobReply Reply( JobAndChannel.GetJob() );
	
	std::stringstream Error;
	Array<char> Binary;
	
	//	gr: if it's binary on the outside (from the channel), we need to decode once
	if ( DataParam.GetFormat().GetFirstContainer() == Soy::GetTypeName<Array<char>>() )
		DataParam.DecodeFirstContainer();

	//	gr: problem with algo? file/gzip/binary, decode to binary, just decodes file to binary...
	//		need to decide whether to decode all, or decode asap...
	while ( DataParam.DecodeFirstContainer() )
	{
	}
	
	if ( !DataParam.Decode( Binary ) )
	{
		Error << "failed to decode to binary" << std::endl;
	}
	else
	{
		TProtoBuf ProtoBuf( GetArrayBridge(Binary) );
		std::stringstream Output;
		Output << "decoded to " << Binary.GetDataSize() << " bytes. ";
		
		int Remainder = Binary.GetDataSize() % sizeof(uint32);
		Output << "32 bit remainder; " << Remainder;
		
		Reply.mParams.AddDefaultParam( Output.str() );
		
	}
	
	if ( !Error.str().empty() )
		Reply.mParams.AddErrorParam( Error.str() );
	

	JobAndChannel.GetChannel().SendJobReply( Reply );
}

