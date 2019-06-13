#include "Router.h"
#include <boost/format.hpp>

namespace Godec {

std::string RouterComponent::SlotRoutingStream = "routing_stream";
std::string RouterComponent::SlotToRouteStream = "stream_to_route";
std::string RouterComponent::SlotRoutedOutputStreamedPrefix = "output_stream";

LoopProcessor* RouterComponent::make(std::string id, ComponentGraphConfig* configPt) {
    return new RouterComponent(id, configPt);
}
std::string RouterComponent::describeThyself() {
    return "Splits an incoming stream into separate streams, based on the binary decision of another input stream";
}

/* RouterComponent::ExtendedDescription
The router component splits an incoming stream into several streams ("num_outputs"), with two major modes:

"sad_nbest": "routing_stream" is expected to be an NbestDecoderMessage, where the 0-th nbest entry is expected to contain a sequence of 0 (nonspeech) or 1 (speech), which the router will use to route the stream

"utterance_round_robin": Simple round robin on an utterance-by-utterance basis (pass in ConversationStateDecoerMessage for "routing_stream")

Very important note: The output streams have completely new timestamps, that's why for each "output_stream_" stream there is a corresponding "conversation_state_" for that stream. If you from there on you only care about these downstream streams, there is no issue, HOWEVER, you can't combine one of these streams with another stream from further upstream, since they have entirely different timestamps. If you WANT to combine things again, you need to re-merge these redefined streams together with the `Merger` component. The `Merger` component takes in the `time_map` stream which tells it how to combine the separate stream into one original one again.

One more subtle note: At the end of a "conversation" (as defined by the ConversationStateDecoderMessage), in order for all substreams to see this signal, the Router holds off a small amount so it can spread it among all streams at the end with that signal. When you re-merge the streams with the `Merger` component, this will result in a sequence of very short utterances that the downstream components should be able to deal with. This is an obscure edge case that only occurs at the of conversations.
*/

RouterComponent::RouterComponent(std::string id, ComponentGraphConfig* configPt) :
    LoopProcessor(id, configPt) {
    std::list<std::string> requiredOutputSlots;
    std::string router_type_string = configPt->get<std::string>("router_type", "Type of routing. Valid values: 'sad_nbest', 'utterance_round_robin'");
    if (router_type_string == "sad_nbest") {
        mMode = Mode::SadNbest;
        mNumRoutes = 2;
    } else if (router_type_string == "utterance_round_robin") {
        mMode = Mode::UtteranceRoundRobin;
        mNumRoutes = configPt->get<int>("num_outputs", "Number of outputs to distribute to");
    } else GODEC_ERR << "Unknown router_type '" << router_type_string << "'" << std::endl;

    addInputSlotAndUUID(SlotRoutingStream, UUID_AnyDecoderMessage);
    addInputSlotAndUUID(SlotToRouteStream, UUID_AnyDecoderMessage);

    for (int routeIdx = 0; routeIdx < mNumRoutes; routeIdx++) {
        requiredOutputSlots.push_back(SlotRoutedOutputStreamedPrefix + "_" + (boost::format("%1%") % routeIdx).str()); // GodecDocIgnore
        // .push_back(Slot: output_stream_[0-9]);  // replacement for above godec doc ignore
        requiredOutputSlots.push_back(SlotConversationState + "_" + (boost::format("%1%") % routeIdx).str()); // GodecDocIgnore
        // .push_back(Slot: conversation_state_[0-9]);  // replacement for above godec doc ignore
        mRoutedStreamOffsets.push_back(-1);
        mCurrentUttIdByRoute.push_back("");
    }
    requiredOutputSlots.push_back(SlotTimeMap);

    initOutputs(requiredOutputSlots);

    mToRouteStreamOffset = -1;
}

RouterComponent::~RouterComponent() {
}

void RouterComponent::ProcessMessage(const DecoderMessageBlock& msgBlock) {
    auto convStateMsg = msgBlock.get<ConversationStateDecoderMessage>(SlotConversationState);

    // Clone the message and cast to non-const because we will be slicing it up. We don't want to affect the original message
    auto baseToRouteMsg = msgBlock.getBaseMsg(SlotToRouteStream)->clone();

    if (mMode == Mode::SadNbest) {
        // Stick new stuff at the end of the message accumulators
        auto nbestRoutingMsg = msgBlock.get<NbestDecoderMessage>(SlotRoutingStream);
        std::copy(nbestRoutingMsg->mWords[0].begin(), nbestRoutingMsg->mWords[0].end(), std::back_inserter(mAccumTop1));
        std::copy(nbestRoutingMsg->mAlignment[0].begin(), nbestRoutingMsg->mAlignment[0].end(), std::back_inserter(mAccumAlignment));
        // This loop just builds up the remaining message accumulators, so we can conveniently step through the nbest entry one-by-one
        for(int idx = 0; idx < nbestRoutingMsg->mWords[0].size(); idx++) {
            // This may look a bit odd that we keep packing the same messages onto these vectors for each nbest entry, but we are relying on the fact that these are actually message *pointer* that all then point to the same message. If we slice a part out of one, we slice out of the others as well, thus making sure that whatever message we are looking at in the "distribution loop" below represents the updated remainder.
            mAccumToRouteBaseMsg.push_back(baseToRouteMsg);
            mAccumEndOfUtt.push_back((convStateMsg->mLastChunkInUtt && (idx == nbestRoutingMsg->mWords[0].size() -1)) ? true : false);
            mAccumUttId.push_back(convStateMsg->mUtteranceId);
            mAccumEndOfConvo.push_back((convStateMsg->mLastChunkInConvo && (idx == nbestRoutingMsg->mWords[0].size() -1)) ? true : false);
            mAccumConvoId.push_back(convStateMsg->mConvoId);

            // This is the special provision mentioned in the component description: The problem is, when we witness an end-of-convo, whatever is the current state (speech or noise), the OTHER one already saw its last message, so there is no direct way of informing that stream that the conversation has ended (we can't hold off data for that stream either, since that would mean indefinite latency for it).
            // The only solution we could think of: Try to slice off the smallest possible chunk from the to-route stream and send it to the other stream so it can carry the end-of-convo signal
            // The way we do it here in the code is by inserting a dummy end-of-convo at the closest sliceable point
            if (convStateMsg->mLastChunkInConvo && (idx == nbestRoutingMsg->mWords[0].size() -1)) {
                uint64_t sliceTime = convStateMsg->getTime()-1;
                std::vector<DecoderMessage_ptr> tmp{baseToRouteMsg};
                auto nonConstBaseToRouteMsg = boost::const_pointer_cast<DecoderMessage>(baseToRouteMsg);
                while(sliceTime >= 0 && !nonConstBaseToRouteMsg->canSliceAt(sliceTime, tmp, 0, false)) {
                    sliceTime--;
                }
                if (sliceTime == 0) GODEC_ERR << "Router: Could not find a spot to insert a dummy end-of-convo signal. Please confer with the component documentation what that means";
                mAccumAlignment.insert(mAccumAlignment.end()-1, sliceTime);
                mAccumTop1.insert(mAccumTop1.end()-1, mAccumTop1.back() == 1 ? 0: 1);
                mAccumToRouteBaseMsg.insert(mAccumToRouteBaseMsg.end()-1, baseToRouteMsg);
                mAccumEndOfUtt.insert(mAccumEndOfUtt.end()-1, true);
                mAccumUttId.insert(mAccumUttId.end()-1, mAccumUttId.back());
                mAccumUttId.back() = mAccumUttId.back()+"_dummy";
                mAccumEndOfConvo.insert(mAccumEndOfConvo.end()-1, true);
                mAccumConvoId.insert(mAccumConvoId.end()-1, mAccumConvoId.back());
            }

        }
        // Now distribute
        while(!mAccumTop1.empty()) {

            // If this is the only element and it's not a forced utt-end due to convstate, we have to defer until more data comes in
            if (mAccumTop1.size() == 1 && !mAccumEndOfUtt.front()) break;

            uint64_t sliceTime = mAccumAlignment.front();
            int64_t sliceLength = sliceTime - mToRouteStreamOffset;
            int routeIdx = mAccumTop1.front();

            // Construct time map
            TimeMapEntry timeMapEntry;
            timeMapEntry.startOrigTime = mToRouteStreamOffset+1;
            timeMapEntry.endOrigTime = mAccumAlignment.front();
            timeMapEntry.startMappedTime = mRoutedStreamOffsets[routeIdx]+1;
            timeMapEntry.endMappedTime = timeMapEntry.startMappedTime+sliceLength-1;
            timeMapEntry.routeIndex = routeIdx;
            mRoutedStreamOffsets[routeIdx] = timeMapEntry.endMappedTime;
            DecoderMessage_ptr timeMapMsg = TimeMapDecoderMessage::create(sliceTime, timeMapEntry);
            pushToOutputs(SlotTimeMap, timeMapMsg);

            // Get sliced to-route message and send
            std::vector<DecoderMessage_ptr> baseToRouteMsgVector{baseToRouteMsg};
            DecoderMessage_ptr slicedMsg;
            auto baseToRouteMsgNonConstPtr = const_cast<DecoderMessage*>(mAccumToRouteBaseMsg.front().get());
            baseToRouteMsgNonConstPtr->sliceOut(sliceTime, slicedMsg, baseToRouteMsgVector, mToRouteStreamOffset, isVerbose());
            auto nonConstSlicedMsg = boost::const_pointer_cast<DecoderMessage>(slicedMsg);
            nonConstSlicedMsg->shiftInTime((int64_t)timeMapEntry.endMappedTime-(int64_t)slicedMsg->getTime());
            pushToOutputs(SlotRoutedOutputStreamedPrefix +"_" + (boost::format("%1%") % routeIdx).str(), slicedMsg);

            if (mCurrentUttIdByRoute[routeIdx] == "") {
                mCurrentUttIdByRoute[routeIdx] = mAccumUttId.front() + "_" + (boost::format("%1%") % timeMapEntry.startOrigTime).str();
            }
            bool lastChunkInUtt = mAccumEndOfUtt.front();
            std::string convoId = mAccumConvoId.front();
            bool lastChunkInConvo = mAccumEndOfConvo.front();

            DecoderMessage_ptr convMsg = ConversationStateDecoderMessage::create(nonConstSlicedMsg->getTime(), mCurrentUttIdByRoute[routeIdx], lastChunkInUtt, convoId, lastChunkInConvo);
            pushToOutputs(SlotConversationState + "_" + (boost::format("%1%") % routeIdx).str(), convMsg);

            mAccumTop1.erase(mAccumTop1.begin());
            mAccumAlignment.erase(mAccumAlignment.begin());
            mAccumToRouteBaseMsg.erase(mAccumToRouteBaseMsg.begin());
            mAccumEndOfUtt.erase(mAccumEndOfUtt.begin());
            mAccumUttId.erase(mAccumUttId.begin());
            mAccumEndOfConvo.erase(mAccumEndOfConvo.begin());
            mAccumConvoId.erase(mAccumConvoId.begin());

            mToRouteStreamOffset = sliceTime;

            if (lastChunkInUtt) mCurrentUttIdByRoute[routeIdx] = "";
        }
    } else if (mMode == Mode::UtteranceRoundRobin) {
        GODEC_ERR << getLPId() << "UtteranceRoundRobin is right now not implemented";
    }
}

}
