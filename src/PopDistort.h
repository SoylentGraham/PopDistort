#pragma once
#include <ofxSoylent.h>
#include <SoyApp.h>
#include <TJob.h>
#include <TChannel.h>

template <> template<>
bool SoyData_Impl<std::string>::Encode(const SoyData_Impl<vec2f>& Data);



class TPopDistort : public TJobHandler, public TChannelManager
{
public:
	TPopDistort();
	
	virtual void	AddChannel(std::shared_ptr<TChannel> Channel) override;

	void			OnFindHole(TJobAndChannel& JobAndChannel);
	
public:
	Soy::Platform::TConsoleApp	mConsoleApp;
};



