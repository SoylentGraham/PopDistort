#pragma once
#include <ofxSoylent.h>
#include <SoyApp.h>
#include <TJob.h>
#include <TChannel.h>




class TPopFeatures : public TJobHandler, public TChannelManager
{
public:
	TPopFeatures();
	
	virtual void	AddChannel(std::shared_ptr<TChannel> Channel) override;

	void			OnExit(TJobAndChannel& JobAndChannel);
	void			OnGetFeature(TJobAndChannel& JobAndChannel);
	void			OnFindFeature(TJobAndChannel& JobAndChannel);
	void			OnTrackFeatures(TJobAndChannel& JobAndChannel);
	void			OnFindInterestingFeatures(TJobAndChannel& JobAndChannel);
	void			OnNewFrame(TJobAndChannel& JobAndChannel);
	
	void			OnSendPing(TJobAndChannel& JobAndChannel);
	void			OnRePing(TJobAndChannel& JobAndChannel);

	void			OnDecode(TJobAndChannel& JobAndChannel);
	
public:
	Soy::Platform::TConsoleApp	mConsoleApp;
};



