#include "ZMPSeqItem.h"
#include "BodyItem.h"
#include "BodyMotionItem.h"
#include "BodyMotionEngine.h"
#include <cnoid/LeggedBodyHelper>
#include <cnoid/ItemManager>
#include <cnoid/PutPropertyFunction>
#include <cnoid/Format>
#include "gettext.h"

using namespace std;
using namespace cnoid;

namespace {

class ZMPSeqEngine : public TimeSyncItemEngine
{
    shared_ptr<ZMPSeq> seq;
    LeggedBodyHelperPtr legged;
    ScopedConnection connection;
    
public:
    ZMPSeqEngine(ZMPSeqItem* seqItem, BodyItem* bodyItem)
        : TimeSyncItemEngine(seqItem),
          seq(seqItem->zmpseq())
    {
        legged = getLeggedBodyHelper(bodyItem->body());
        connection = seqItem->sigUpdated().connect([this](){ refresh(); });
    }

    virtual bool onTimeChanged(double time) override
    {
        bool isValidTime = false;
        if(legged->isValid()){
            if(!seq->empty()){
                const Vector3& zmp = seq->at(seq->clampFrameIndex(seq->frameOfTime(time), isValidTime));
                if(seq->isRootRelative()){
                    legged->setZmp(legged->body()->rootLink()->T() * zmp, true);
                } else {
                    legged->setZmp(zmp, true);
                }
            }
        }
        return isValidTime;
    }
};

}


void ZMPSeqItem::initializeClass(ExtensionManager* ext)
{
    ext->itemManager().registerClass<ZMPSeqItem, Vector3SeqItem>(N_("ZMPSeqItem"));

    auto& contentName = ZMPSeq::seqContentName();
    
    BodyMotionItem::registerExtraSeqContent(
        contentName,
        [](std::shared_ptr<AbstractSeq> seq) -> AbstractSeqItem* {
            if(auto zmpseq = dynamic_pointer_cast<ZMPSeq>(seq)){
                return new ZMPSeqItem(zmpseq);
            }
            return nullptr;
        });
    
    BodyMotionEngine::registerExtraSeqEngineFactory(
        contentName,
        [](BodyItem* bodyItem, AbstractSeqItem* seqItem) -> TimeSyncItemEngine* {
            if(auto item = dynamic_cast<ZMPSeqItem*>(seqItem)){
                return new ZMPSeqEngine(item, bodyItem);
            }
            return nullptr;
        });
}


ZMPSeqItem::ZMPSeqItem()
    : Vector3SeqItem(std::make_shared<ZMPSeq>())
{
    zmpseq_ = static_pointer_cast<ZMPSeq>(seq());
}


ZMPSeqItem::ZMPSeqItem(std::shared_ptr<ZMPSeq> seq)
    : Vector3SeqItem(seq),
      zmpseq_(seq)
{

}


ZMPSeqItem::ZMPSeqItem(const ZMPSeqItem& org)
    : Vector3SeqItem(org, std::make_shared<ZMPSeq>(*org.zmpseq_))
{
    zmpseq_ = static_pointer_cast<ZMPSeq>(seq());
}


ZMPSeqItem::~ZMPSeqItem()
{

}


bool ZMPSeqItem::makeRootRelative(bool on)
{
    BodyMotionItem* bodyMotionItem = parentItem<BodyMotionItem>();
    auto& os = mvout(false);
    if(bodyMotionItem){
        if(cnoid::makeRootRelative(*zmpseq_, *bodyMotionItem->motion(), on)){
            os << formatR(_("{0} of {1} has been converted to {2}."),
                          displayName(), bodyMotionItem->displayName(),
                          (on ? _("the root relative coordinate") : _("the global coordinate")))
               << endl;
            return true;
        }
    }
    os << formatR(_("{0}'s coordinate system cannot be changed "
                    "because there is no root link motion associated with {0}."),
                  displayName()) << endl;
    return false;
}


Item* ZMPSeqItem::doDuplicate() const
{
    return new ZMPSeqItem(*this);
}


void ZMPSeqItem::doPutProperties(PutPropertyFunction& putProperty)
{
    AbstractSeqItem::doPutProperties(putProperty);
    putProperty(_("Root relative"), zmpseq_->isRootRelative(),
                [this](bool on){ return makeRootRelative(on); });
}
