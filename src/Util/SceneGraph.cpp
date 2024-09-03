#include "SceneGraph.h"
#include "SceneNodeClassRegistry.h"
#include "CloneMap.h"
#include "ValueTree.h"
#include "UTF8.h"
#include "Format.h"
#include <cnoid/stdx/filesystem>
#include <unordered_map>
#include <typeindex>
#include <mutex>
#include <stdexcept>

using namespace std;
using namespace cnoid;
namespace filesystem = stdx::filesystem;

namespace {

// Id to access the correspondingCloneMap flag
CloneMap::FlagId DisableNonNodeCloning("SgObjectDisableNonNodeCloning");
CloneMap::FlagId DisableMetaSceneCloning("SgObjectDisableMetaSceneCloning");

const BoundingBox emptyBoundingBox;

}


SgObject::SgObject()
{
    attributes_ = 0;
    hasValidBoundingBoxCache_ = false;
}


SgObject::SgObject(const SgObject& org)
    : attributes_(org.attributes_),
      hasValidBoundingBoxCache_(false),
      name_(org.name_)
{
    if(org.uriInfo){
        uriInfo.reset(new UriInfo(*org.uriInfo));
    }
}


Referenced* SgObject::doClone(CloneMap*) const
{
    return new SgObject(*this);
}


void SgObject::setNonNodeCloning(CloneMap& cloneMap, bool on)
{
    cloneMap.setFlag(DisableNonNodeCloning, !on);
}


bool SgObject::checkNonNodeCloning(const CloneMap& cloneMap)
{
    return !cloneMap.flag(DisableNonNodeCloning);
}


void SgObject::setMetaSceneCloning(CloneMap& cloneMap, bool on)
{
    cloneMap.setFlag(DisableMetaSceneCloning, !on);
}


bool SgObject::checkMetaSceneCloning(const CloneMap& cloneMap)
{
    return !cloneMap.flag(DisableMetaSceneCloning);
}


int SgObject::numChildObjects() const
{
    return 0;
}


SgObject* SgObject::childObject(int /* index */)
{
    return nullptr;
}


SgObject* SgObject::findObject_(std::function<bool(SgObject* object)>& pred)
{
    if(pred(this)){
        return this;
    }
    int n = numChildObjects();
    for(int i=0; i < n; ++i){
        if(auto found = childObject(i)->findObject_(pred)){
            return found;
        }
    }
    return nullptr;
}


bool SgObject::traverseObjects_(std::function<TraverseStatus(SgObject* object)>& pred)
{
    auto status = pred(this);
    if(status == Stop){
        return false;
    }
    if(status == Next){
        return true;
    }
    int n = numChildObjects();
    for(int i=0; i < n; ++i){
        if(!childObject(i)->traverseObjects_(pred)){
            return false;
        }
    }
    return true;
}


void SgObject::notifyUpperNodesOfUpdate(SgUpdate& update)
{
    notifyUpperNodesOfUpdate(update, update.hasAction(SgUpdate::GeometryModified));
}


void SgObject::notifyUpperNodesOfUpdate(SgUpdate& update, bool doInvalidateBoundingBox)
{
    update.pushNode(this);
    if(doInvalidateBoundingBox){
        invalidateBoundingBox();
    }
    sigUpdated_(update);
    for(const_parentIter p = parents.begin(); p != parents.end(); ++p){
        (*p)->notifyUpperNodesOfUpdate(update, doInvalidateBoundingBox);
    }
    update.popNode();
}


void SgObject::addParent(SgObject* parent, SgUpdateRef update)
{
    parents.insert(parent);

    if(update){
        update->clearPath();
        update->pushNode(this);
        parent->notifyUpperNodesOfUpdate(
            update->withAction(SgUpdate::Added), hasAttribute(Geometry));
    }

    if(parents.size() == 1){
        sigGraphConnection_(true);
    }
}


void SgObject::removeParent(SgObject* parent)
{
    parents.erase(parent);
    if(parents.empty()){
        sigGraphConnection_(false);
    }
}


bool SgObject::checkIfAncestorOf(SgObject* obj) const
{
    if(obj == this){
        return true;
    }
    auto it = obj->parentBegin();
    while(it != obj->parentEnd()){
        if(checkIfAncestorOf(*it)){
            return true;
        }
        ++it;
    }
    return false;
}


const std::string& SgObject::uri() const
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    return uriInfo->uri;
}


std::string SgObject::localFilePath() const
{
    if(uriInfo){
        if(uriInfo->uri.compare(0, 7, "file://") == 0){
            return uriInfo->uri.substr(7);
        } else {
            return uriInfo->uri;
        }
    }
    return string();
}


const std::string& SgObject::absoluteUri() const
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    return uriInfo->absoluteUri;
}


std::string SgObject::localFileAbsolutePath() const
{
    if(uriInfo){
        if(uriInfo->absoluteUri.compare(0, 7, "file://") == 0){
            return uriInfo->absoluteUri.substr(7);
        }
    }
    return string();
}


const std::string& SgObject::uriObjectName() const
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    return uriInfo->objectName;
}


const std::string& SgObject::uriFragment() const
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    return uriInfo->fragment;
}


Mapping* SgObject::uriMetadata() const
{
    if(uriInfo){
        return static_cast<Mapping*>(uriInfo->metadata.get());
    }
    return nullptr;
}


void SgObject::setUriWithFilePathAndBaseDirectory
(const std::string& filePath, const std::string& baseDirectory)
{
    filesystem::path path(fromUTF8(filePath));
    if(path.is_relative()){
        filesystem::path baseDirPath(fromUTF8(baseDirectory));
        if(baseDirPath.is_relative()){
            baseDirPath = filesystem::current_path() / baseDirPath;
        }
        path = baseDirPath / path;
    }
    setUri(filePath, toUTF8(path.generic_string()));
}


void SgObject::setUriByFilePathAndBaseDirectory
(const std::string& filePath, const std::string& baseDirectory)
{
    setUriWithFilePathAndBaseDirectory(filePath, baseDirectory);
}


void SgObject::setUriWithFilePathAndCurrentDirectory(const std::string& filePath)
{
    filesystem::path path(fromUTF8(filePath));
    if(path.is_relative()){
        path = filesystem::current_path() / path;
    }
    setUri(filePath, toUTF8(path.generic_string()));
}


void SgObject::setUriByFilePathAndCurrentDirectory(const std::string& filePath)
{
    setUriWithFilePathAndCurrentDirectory(filePath);
}


void SgObject::setUri(const std::string& uri, const std::string& absoluteUri)
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    uriInfo->uri = uri;
    if(absoluteUri.compare(0, 7, "file://") == 0){
        uriInfo->absoluteUri = absoluteUri;
    } else {
        uriInfo->absoluteUri = formatC("file://{0}", absoluteUri);
    }
}


void SgObject::setUriObjectName(const std::string& name)
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    uriInfo->objectName = name;
}


void SgObject::setUriFragment(const std::string& fragment)
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    uriInfo->fragment = fragment;
}


void SgObject::setUriMetadata(Mapping* data)
{
    if(!uriInfo){
        uriInfo.reset(new UriInfo);
    }
    uriInfo->metadata = data;
}


int SgNode::findSuperClassId(int classId)
{
    return SceneNodeClassRegistry::instance().getSuperClassId(classId);
}


int SgNode::findClassId(const std::type_info& nodeType)
{
    return SceneNodeClassRegistry::instance().getClassId(nodeType);
}


int SgNode::registerNodeType(const std::type_info& nodeType, const std::type_info& superType)
{
    return SceneNodeClassRegistry::instance().registerClassAsTypeInfo(nodeType, superType);
}


SgNode::SgNode()
{
    setAttribute(Node);
    classId_ = findClassId<SgNode>();
    decorationRefCounter = 0;
}


SgNode::SgNode(int classId)
    : classId_(classId)
{
    setAttribute(Node);
    decorationRefCounter = 0;
}


SgNode::SgNode(const SgNode& org)
    : SgObject(org),
      classId_(org.classId_)
{
    decorationRefCounter = 0;
}


SgNode::~SgNode()
{

}


Referenced* SgNode::doClone(CloneMap*) const
{
    return new SgNode(*this);
}


std::string SgNode::className() const
{
    return SceneNodeClassRegistry::instance().getClassName(classId_);
}


const BoundingBox& SgNode::boundingBox() const
{
    return emptyBoundingBox;
}


const BoundingBox& SgNode::untransformedBoundingBox() const
{
    return boundingBox();
}


/**
   \note The current implementation of this function does not seem to return the correct T value
*/
static bool findNodeSub(SgNode* node, const std::string& name, SgNodePath& path, Affine3 T, Affine3& out_T)
{
    path.push_back(node);

    if(auto group = dynamic_cast<SgGroup*>(node)){
        if(auto transform = dynamic_cast<SgTransform*>(group)){
            Affine3 T0;
            transform->getTransform(T0);
            T = T * T0;
        }
        if(node->name() == name){
            out_T = T;
            return true;
        }
        for(auto& child : *group){
            if(findNodeSub(child, name, path, T, out_T)){
                return true;
            }
        }
    } else {
        if(node->name() == name){
            out_T = T;
            return true;
        }
    }
    
    path.pop_back();

    return false;
}


SgNodePath SgNode::findNode(const std::string& name, Affine3& out_T)
{
    SgNodePath path;
    out_T.setIdentity();
    findNodeSub(this, name, path, out_T, out_T);
    return path;
}


bool SgNode::traverseNodes_(std::function<TraverseStatus(SgNode* node)>& pred)
{
    auto status = pred(this);
    if(status == Stop){
        return false;
    }
    if(status == Next){
        return true;
    }
    if(auto group = toGroupNode()){
        for(auto& child : *group){
            if(!child->traverseNodes_(pred)){
                return false;
            }
        }
    }
    return true;
}


SgGroup::SgGroup()
    : SgNode(findClassId<SgGroup>())
{
    setAttribute(GroupNode);
}


SgGroup::SgGroup(int classId)
    : SgNode(classId)
{
    setAttribute(GroupNode);
}


SgGroup::SgGroup(const SgGroup& org, CloneMap* cloneMap)
    : SgNode(org)
{
    children.reserve(org.numChildren());

    if(cloneMap){
        // deep copy
        if(checkMetaSceneCloning(*cloneMap)){
            for(auto& child : org){
                addChild(cloneMap->getClone<SgNode>(child));
            }
        } else {
            for(auto& child : org){
                if(!child->hasAttribute(MetaScene)){
                    addChild(cloneMap->getClone<SgNode>(child));
                }
            }
        }
    }

    if(org.hasValidBoundingBoxCache()){
        bboxCache = org.bboxCache;
        setBoundingBoxCacheReady();
    }
}


SgGroup::~SgGroup()
{
    for(const_iterator p = begin(); p != end(); ++p){
        (*p)->removeParent(this);
    }
}


Referenced* SgGroup::doClone(CloneMap* cloneMap) const
{
    return new SgGroup(*this, cloneMap);
}


int SgGroup::numChildObjects() const
{
    return children.size();
}


SgObject* SgGroup::childObject(int index)
{
    return children[index].get();
}


const BoundingBox& SgGroup::boundingBox() const
{
    if(hasValidBoundingBoxCache()){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        auto& node = *p;
        if(!node->hasAttribute(Marker)){
            bboxCache.expandBy(node->boundingBox());
        }
    }
    setBoundingBoxCacheReady();

    return bboxCache;
}


bool SgGroup::contains(SgNode* node) const
{
    for(const_iterator p = begin(); p != end(); ++p){
        if((*p) == node){
            return true;
        }
    }
    return false;
}


int SgGroup::findChildIndex(SgNode* child) const
{
    for(size_t i=0; i < children.size(); ++i){
        if(children[i] == child){
            return i;
        }
    }
    return -1;
}


void SgGroup::addChild(SgNode* node, SgUpdateRef update)
{
    if(node){
        children.push_back(node);
        node->addParent(this, update);
    }
}


bool SgGroup::addChildOnce(SgNode* node, SgUpdateRef update)
{
    if(!contains(node)){
        addChild(node, update);
        return true;
    }
    return false;
}


void SgGroup::insertChild(int index, SgNode* node, SgUpdateRef update)
{
    if(node){
        if(index > static_cast<int>(children.size())){
            index = children.size();
        }
        children.insert(children.begin() + index, node);

        node->addParent(this, update);
    }
}


void SgGroup::insertChild(SgNode* nextNode, SgNode* node, SgUpdateRef update)
{
    int index = findChildIndex(nextNode);
    if(index >= 0){
        insertChild(index, node, update);
    } else {
        insertChild(0, node, update);
    }
}


void SgGroup::setSingleChild(SgNode* node, SgUpdateRef update)
{
    int n = numChildren();
    if(n > 0){
        bool found = false;
        for(int i = n - 1; i >= 0; --i){
            if(child(i) == node && !found){
                found = true;
                continue;
            }
            removeChildAt(i, update);
        }
        if(!empty()){
            return;
        }
    }
    addChild(node, update);
}


SgGroup::iterator SgGroup::removeChild(iterator childIter, SgUpdateRef update)
{
    iterator next;
    SgNode* child = *childIter;
    child->removeParent(this);
    
    if(!update){
        next = children.erase(childIter);
    } else {
        SgNodePtr childHolder = child;
        next = children.erase(childIter);
        update->clearPath();
        update->pushNode(child);
        notifyUpperNodesOfUpdate(
            update->withAction(SgUpdate::Removed), child->hasAttribute(Geometry));
    }
    return next;
}


bool SgGroup::removeChild(SgNode* node, SgUpdateRef update)
{
    bool removed = false;
    if(node){
        iterator p = children.begin();
        while(p != children.end()){
            if((*p) == node){
                p = removeChild(p, update);
                removed = true;
            } else {
                ++p;
            }
        }
    }
    return removed;
}


void SgGroup::removeChildAt(int index, SgUpdateRef update)
{
    removeChild(children.begin() + index, update);
}


void SgGroup::clearChildren(SgUpdateRef update)
{
    iterator p = children.begin();
    while(p != children.end()){
        p = removeChild(p, update);
    }
}


void SgGroup::copyChildrenTo(SgGroup* group, SgUpdateRef update)
{
    for(size_t i=0; i < children.size(); ++i){
        group->addChild(child(i), update);
    }
}


void SgGroup::moveChildrenTo(SgGroup* group, SgUpdateRef update)
{
    const int destTop = group->children.size();
    
    for(size_t i=0; i < children.size(); ++i){
        group->addChild(child(i));
    }
    clearChildren(update);
    
    if(update){
        update->setAction(SgUpdate::Added);
        for(int i=destTop; i < group->numChildren(); ++i){
            update->clearPath();
            group->child(i)->notifyUpdate(*update);
        }
    }
}


void SgGroup::insertChainedGroup(SgGroup* group, SgUpdateRef update)
{
    moveChildrenTo(group);
    addChild(group);
    if(update){
        update->addAction(SgUpdate::Added);
        group->notifyUpdate(*update);
    }
}


SgGroup* SgGroup::nextChainedGroup()
{
    SgGroup* nextGroup = nullptr;
    if(children.size() == 1){
        nextGroup = dynamic_cast<SgGroup*>(children.front().get());
    }
    return nextGroup;
}


void SgGroup::removeChainedGroup(SgGroup* group, SgUpdateRef update)
{
    SgGroup* parent = this;
    auto next = nextChainedGroup();
    while(next){
        if(next == group){
            parent->removeChild(group);
            group->moveChildrenTo(parent);
            if(update){
                update->addAction(SgUpdate::Removed);
                update->clearPath();
                update->pushNode(group);
                notifyUpperNodesOfUpdate(*update);
            }
            break;
        }
        next = next->nextChainedGroup();
    }
}


void SgGroup::throwTypeMismatchError()
{
    throw std::runtime_error("type mismatch");
}


SgInvariantGroup::SgInvariantGroup()
    : SgGroup(findClassId<SgInvariantGroup>())
{

}


SgInvariantGroup::SgInvariantGroup(const SgInvariantGroup& org, CloneMap* cloneMap)
    : SgGroup(org, cloneMap)
{

}


Referenced* SgInvariantGroup::doClone(CloneMap* cloneMap) const
{
    return new SgInvariantGroup(*this, cloneMap);
}


SgTransform::SgTransform(int classId)
    : SgGroup(classId)
{
    setAttributes(TransformNode | Geometry);
}


SgTransform::SgTransform(const SgTransform& org, CloneMap* cloneMap)
    : SgGroup(org, cloneMap)
{
    untransformedBboxCache = org.untransformedBboxCache;
}


const BoundingBox& SgTransform::untransformedBoundingBox() const
{
    if(!hasValidBoundingBoxCache()){
        boundingBox();
    }
    return untransformedBboxCache;
}


SgPosTransform::SgPosTransform(int classId)
    : SgTransform(classId),
      T_(Isometry3::Identity())
{

}


SgPosTransform::SgPosTransform()
    : SgPosTransform(findClassId<SgPosTransform>())
{

}


SgPosTransform::SgPosTransform(const Isometry3& T)
    : SgTransform(findClassId<SgPosTransform>()),
      T_(T)
{

}


SgPosTransform::SgPosTransform(const Affine3& T)
    : SgTransform(findClassId<SgPosTransform>()),
      T_(T.matrix())
{

}


SgPosTransform::SgPosTransform(const SgPosTransform& org, CloneMap* cloneMap)
    : SgTransform(org, cloneMap),
      T_(org.T_)
{

}


Referenced* SgPosTransform::doClone(CloneMap* cloneMap) const
{
    return new SgPosTransform(*this, cloneMap);
}


const BoundingBox& SgPosTransform::boundingBox() const
{
    if(hasValidBoundingBoxCache()){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        auto& node = *p;
        if(!node->hasAttribute(Marker)){
            bboxCache.expandBy(node->boundingBox());
        }
    }
    untransformedBboxCache = bboxCache;
    bboxCache.transform(T_);
    setBoundingBoxCacheReady();
    return bboxCache;
}


void SgPosTransform::getTransform(Affine3& out_T) const
{
    out_T = T_;
}


SgScaleTransform::SgScaleTransform(int classId)
    : SgTransform(classId)
{
    scale_.setOnes();
}


SgScaleTransform::SgScaleTransform()
    : SgScaleTransform(findClassId<SgScaleTransform>())
{

}


SgScaleTransform::SgScaleTransform(double scale)
    : SgTransform(findClassId<SgScaleTransform>()),
      scale_(scale, scale, scale)
{

}


SgScaleTransform::SgScaleTransform(const Vector3& scale)
    : SgTransform(findClassId<SgScaleTransform>()),
      scale_(scale)
{

}


SgScaleTransform::SgScaleTransform(const SgScaleTransform& org, CloneMap* cloneMap)
    : SgTransform(org, cloneMap),
      scale_(org.scale_)
{

}


Referenced* SgScaleTransform::doClone(CloneMap* cloneMap) const
{
    return new SgScaleTransform(*this, cloneMap);
}
        

const BoundingBox& SgScaleTransform::boundingBox() const
{
    if(hasValidBoundingBoxCache()){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        auto& node = *p;
        if(!node->hasAttribute(Marker)){
            bboxCache.expandBy(node->boundingBox());
        }
    }
    untransformedBboxCache = bboxCache;
    bboxCache.transform(Affine3(scale_.asDiagonal()));
    setBoundingBoxCacheReady();
    return bboxCache;
}


void SgScaleTransform::getTransform(Affine3& out_T) const
{
    out_T = scale_.asDiagonal();
}


SgAffineTransform::SgAffineTransform(int classId)
    : SgTransform(classId),
      T_(Affine3::Identity())
{

}


SgAffineTransform::SgAffineTransform()
    : SgAffineTransform(findClassId<SgAffineTransform>())
{

}


SgAffineTransform::SgAffineTransform(const Affine3& T)
    : SgTransform(findClassId<SgAffineTransform>()),
      T_(T)
{

}


SgAffineTransform::SgAffineTransform(const SgAffineTransform& org, CloneMap* cloneMap)
    : SgTransform(org, cloneMap),
      T_(org.T_)
{

}


Referenced* SgAffineTransform::doClone(CloneMap* cloneMap) const
{
    return new SgAffineTransform(*this, cloneMap);
}


const BoundingBox& SgAffineTransform::boundingBox() const
{
    if(hasValidBoundingBoxCache()){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        auto& node = *p;
        if(!node->hasAttribute(Marker)){
            bboxCache.expandBy((*p)->boundingBox());
        }
    }
    untransformedBboxCache = bboxCache;
    bboxCache.transform(T_);
    setBoundingBoxCacheReady();
    return bboxCache;
}


void SgAffineTransform::getTransform(Affine3& out_T) const
{
    out_T = T_;
}


SgFixedPixelSizeGroup::SgFixedPixelSizeGroup()
    : SgFixedPixelSizeGroup(1.0)
{

}


SgFixedPixelSizeGroup::SgFixedPixelSizeGroup(double pixelSizeRatio)
    : SgGroup(findClassId<SgFixedPixelSizeGroup>())
{
    pixelSizeRatio_ = pixelSizeRatio;
}
      

SgFixedPixelSizeGroup::SgFixedPixelSizeGroup(int classId)
    : SgGroup(classId)
{
    pixelSizeRatio_ = 1.0;
}


SgFixedPixelSizeGroup::SgFixedPixelSizeGroup(const SgFixedPixelSizeGroup& org, CloneMap* cloneMap)
    : SgGroup(org, cloneMap)
{
    pixelSizeRatio_ = org.pixelSizeRatio_;
}


Referenced* SgFixedPixelSizeGroup::doClone(CloneMap* cloneMap) const
{
    return new SgFixedPixelSizeGroup(*this, cloneMap);
}


SgSwitch::SgSwitch(bool on)
{
    isTurnedOn_ = on;
}


SgSwitch::SgSwitch(const SgSwitch& org)
    : SgObject(org)
{
    isTurnedOn_ = org.isTurnedOn_;
}


Referenced* SgSwitch::doClone(CloneMap* cloneMap) const
{
    return new SgSwitch(*this);
}


void SgSwitch::setTurnedOn(bool on, SgUpdateRef update)
{
    if(on != isTurnedOn_){
        isTurnedOn_ = on;
        if(update){
            notifyUpdate(update->withAction(SgUpdate::Modified));
        }
    }
}


SgSwitchableGroup::SgSwitchableGroup()
    : SgGroup(findClassId<SgSwitchableGroup>())
{
    isTurnedOn_ = true;
}


SgSwitchableGroup::SgSwitchableGroup(SgSwitch* switchObject)
    : SgSwitchableGroup()
{
    setSwitch(switchObject);
}


SgSwitchableGroup::SgSwitchableGroup(const SgSwitchableGroup& org, CloneMap* cloneMap)
    : SgGroup(org, cloneMap)
{
    if(org.switchObject){
        if(cloneMap){
            switchObject = cloneMap->getClone<SgSwitch>(org.switchObject);
        } else {
            switchObject = org.switchObject;
        }
    } 
    isTurnedOn_ = org.isTurnedOn_;
}


SgSwitchableGroup::~SgSwitchableGroup()
{
    if(switchObject){
        switchObject->removeParent(this);
    }
}


void SgSwitchableGroup::setSwitch(SgSwitch* newSwitchObject)
{
    if(switchObject){
        switchObject->removeParent(this);
    }
    switchObject = newSwitchObject;
    if(newSwitchObject){
        newSwitchObject->addParent(this);
    }
}
        
        
Referenced* SgSwitchableGroup::doClone(CloneMap* cloneMap) const
{
    return new SgSwitchableGroup(*this, cloneMap);
}


void SgSwitchableGroup::setTurnedOn(bool on, SgUpdateRef update)
{
    if(switchObject){
        switchObject->setTurnedOn(on, update);

    } else if(on != isTurnedOn_){
        isTurnedOn_ = on;
        if(update){
            notifyUpdate(update->withAction(SgUpdate::Modified));
        }
    }
}


SgUnpickableGroup::SgUnpickableGroup()
    : SgGroup(findClassId<SgUnpickableGroup>())
{

}


SgUnpickableGroup::SgUnpickableGroup(const SgUnpickableGroup& org, CloneMap* cloneMap)
    : SgGroup(org, cloneMap)
{

}


Referenced* SgUnpickableGroup::doClone(CloneMap* cloneMap) const
{
    return new SgUnpickableGroup(*this, cloneMap);
}


SgPreprocessed::SgPreprocessed(int classId)
    : SgNode(classId)
{

}


SgPreprocessed::SgPreprocessed(const SgPreprocessed& org)
    : SgNode(org)
{

}


namespace {

struct NodeClassRegistration {
    NodeClassRegistration() {
        SceneNodeClassRegistry::instance()
            .registerClass<SgNode>("SgNode")
            .registerClass<SgGroup, SgNode>("SgGroup")
            .registerClass<SgInvariantGroup, SgGroup>("SgInvariantGroup")
            .registerClass<SgTransform, SgGroup>("SgTransform")
            .registerClass<SgAffineTransform, SgTransform>("SgAffineTransform")
            .registerClass<SgPosTransform, SgTransform>("SgPosTransform")
            .registerClass<SgScaleTransform, SgTransform>("SgScaleTransform")
            .registerClass<SgFixedPixelSizeGroup, SgGroup>("SgFixedPixelSizeGroup")
            .registerClass<SgSwitchableGroup, SgGroup>("SgSwitchableGroup")
            .registerClass<SgUnpickableGroup, SgGroup>("SgUnpickableGroup")
            .registerClass<SgPreprocessed, SgNode>("SgPreprocessed");
    }
} registration;

}
