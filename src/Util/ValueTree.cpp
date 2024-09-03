#include "ValueTree.h"
#include "UTF8.h"
#include "MathUtil.h"
#include "Format.h"
#include <stack>
#include <iostream>
#include <yaml.h>
#include <cnoid/stdx/filesystem>
#include <functional>
#include "gettext.h"

#ifdef _WIN32
#define snprintf _snprintf_s
#endif

using namespace std;
using namespace cnoid;

namespace {

const bool debugTrace = false;

const char* getTypeName(int typeBits){
    if(typeBits & ValueNode::SCALAR){
        return "scalar";
    } else if(typeBits & ValueNode::MAPPING){
        return "mapping";
    } else if(typeBits & ValueNode::LISTING){
        return "listing";
    } else {
        return "unknown type node";
    }
}

map<string, bool> booleanSymbols;

const char* defaultFloatingNumberFormat = "%g";

ValueNodePtr invalidNode;
MappingPtr invalidMapping;
ListingPtr invalidListing;

constexpr double PI = 3.141592653589793238462643383279502884;
constexpr double TO_RADIAN = PI / 180.0;

void hash_combine(std::size_t& seed, std::size_t hash) {
    seed ^= hash + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

}

ValueNode::Initializer ValueNode::initializer;

ValueNode::Initializer::Initializer()
{
    invalidNode = new ValueNode(INVALID_NODE);
    invalidMapping = new Mapping;
    invalidMapping->typeBits = INVALID_NODE;
    invalidListing = new Listing;
    invalidListing->typeBits = INVALID_NODE;
    
    booleanSymbols["y"] = true;
    booleanSymbols["Y"] = true;
    booleanSymbols["yes"] = true;
    booleanSymbols["Yes"] = true;
    booleanSymbols["YES"] = true;
    booleanSymbols["true"] = true;
    booleanSymbols["True"] = true;
    booleanSymbols["TRUE"] = true;
    booleanSymbols["on"] = true;
    booleanSymbols["On"] = true;
    booleanSymbols["ON"] = true;
    
    booleanSymbols["n"] = false;
    booleanSymbols["N"] = false;
    booleanSymbols["no"] = false;
    booleanSymbols["No"] = false;
    booleanSymbols["NO"] = false;
    booleanSymbols["false"] = false;
    booleanSymbols["False"] = false;
    booleanSymbols["FALSE"] = false;
    booleanSymbols["off"] = false;
    booleanSymbols["Off"] = false;
    booleanSymbols["OFF"] = false;
}


ValueNode::Exception::Exception()
{
    line_ = -1;
    column_ = -1;
}


ValueNode::Exception::~Exception()
{

}


std::string ValueNode::Exception::message() const
{
    if(!message_.empty()){
        if(line_ >= 0){
            return formatR(_("{0} at line {1}, column {2}."), message_, line_, column_);
        } else {
            return formatC("{}.", message_);
        }
    } else {
        if(line_ >= 0){
            return formatR(_("Error at line {0}, column {1}."), line_, column_);
        } else {
            return string();
        }
    }
}


ValueNode::ValueNode(const ValueNode& org)
    : typeBits(org.typeBits),
      line_(org.line_),
      column_(org.column_),
      indexInMapping_(org.indexInMapping_)
{

}


ValueNode* ValueNode::clone() const
{
    return new ValueNode(*this);
}


// disabled
ValueNode& ValueNode::operator=(const ValueNode&)
{
    // throw an exception here ?
    return *this;
}

/*
  void ValueNode::initialize()
  {
  static bool initialized = false;

  if(!initialized){

  invalidNode = new ValueNode(INVALID_NODE);

  invalidMapping = new Mapping;
  invalidMapping->typeBits = INVALID_NODE;

  invalidListing = new Listing;
  invalidListing->typeBits = INVALID_NODE;
        
  booleanSymbols["true"] = true;
  booleanSymbols["yes"] = true;
  booleanSymbols["on"] = true;
  booleanSymbols["false"] = false;
  booleanSymbols["no"] = false;
  booleanSymbols["off"] = false;

  initialized = true;
  }
  }
*/


bool ValueNode::read(int &out_value) const
{
    if(isScalar()){
        const char* nptr = &(static_cast<const ScalarNode* const>(this)->stringValue_[0]);
        char* endptr;
        out_value = strtol(nptr, &endptr, 10);
        if(endptr > nptr){
            return true;
        }
    }
    return false;
}


int ValueNode::toInt() const
{
    if(!isScalar()){
        throwNotScalarException();
    }

    const ScalarNode* const scalar = static_cast<const ScalarNode* const>(this);
    
    const char* nptr = &(scalar->stringValue_[0]);
    char* endptr;
    const int value = strtol(nptr, &endptr, 10);

    if(endptr == nptr){
        ScalarTypeMismatchException ex;
        ex.setPosition(line(), column());
        ex.setMessage(formatR(_("The value \"{}\" must be an integer value"), scalar->stringValue_));
        throw ex;
    }

    return value;
}


bool ValueNode::read(double& out_value) const
{
    if(isScalar()){
        const char* nptr = &(static_cast<const ScalarNode* const>(this)->stringValue_[0]);
        char* endptr;
        out_value = strtod(nptr, &endptr);
        if(endptr > nptr){
            return true;
        }
    }
    return false;
}


bool ValueNode::read(float& out_value) const
{
    if(isScalar()){
        const char* nptr = &(static_cast<const ScalarNode* const>(this)->stringValue_[0]);
        char* endptr;
        out_value = strtof(nptr, &endptr);
        if(endptr > nptr){
            return true;
        }
    }
    return false;
}


double ValueNode::toDouble() const
{
    if(!isScalar()){
        throwNotScalarException();
    }

    const ScalarNode* const scalar = static_cast<const ScalarNode* const>(this);

    const char* nptr = &(scalar->stringValue_[0]);
    char* endptr;
    const double value = strtod(nptr, &endptr);

    if(endptr == nptr){
        ScalarTypeMismatchException ex;
        ex.setPosition(line(), column());
        ex.setMessage(formatR(_("The value \"{}\" must be a floating point number"), scalar->stringValue_));
        throw ex;
    }

    return value;
}


float ValueNode::toFloat() const
{
    if(!isScalar()){
        throwNotScalarException();
    }

    const ScalarNode* const scalar = static_cast<const ScalarNode* const>(this);

    const char* nptr = &(scalar->stringValue_[0]);
    char* endptr;
    const float value = strtof(nptr, &endptr);

    if(endptr == nptr){
        ScalarTypeMismatchException ex;
        ex.setPosition(line(), column());
        ex.setMessage(formatR(_("The value \"{}\" must be a floating point number"), scalar->stringValue_));
        throw ex;
    }

    return value;
}


double ValueNode::toAngle() const
{
    if(!isForcedRadianMode()){
        return TO_RADIAN * toDouble();
    } else {
        return toDouble();
    }
}


bool ValueNode::read(bool& out_value) const
{
    if(isScalar()){
        const ScalarNode* const scalar = static_cast<const ScalarNode* const>(this);
        map<string, bool>::iterator p = booleanSymbols.find(scalar->stringValue_);
        if(p != booleanSymbols.end()){
            out_value = p->second;
            return true;
        }
    }
    return false;
}


bool ValueNode::toBool() const
{
    if(!isScalar()){
        throwNotScalarException();
    }

    const ScalarNode* const scalar = static_cast<const ScalarNode* const>(this);
    map<string, bool>::iterator p = booleanSymbols.find(scalar->stringValue_);
    if(p != booleanSymbols.end()){
        return p->second;
    }
    
    ScalarTypeMismatchException ex;
    ex.setPosition(line(), column());
    ex.setMessage(formatR(_("The value \"{}\" must be a boolean value"), scalar->stringValue_));
    throw ex;
}


bool ValueNode::read(std::string& out_value) const
{
    if(isScalar()){
        out_value = static_cast<const ScalarNode* const>(this)->stringValue_;
        return !out_value.empty();
    }
    return false;
}


const ScalarNode* ValueNode::toScalar() const
{
    if(!isScalar()){
        throwNotScalarException();
    }
    return static_cast<const ScalarNode*>(this);
}


ScalarNode* ValueNode::toScalar()
{
    if(!isScalar()){
        throwNotScalarException();
    }
    return static_cast<ScalarNode*>(this);
}


const Mapping* ValueNode::toMapping() const
{
    if(!isMapping()){
        throwNotMappingException();
    }
    return static_cast<const Mapping*>(this);
}


Mapping* ValueNode::toMapping()
{
    if(!isMapping()){
        throwNotMappingException();
    }
    return static_cast<Mapping*>(this);
}


const Listing* ValueNode::toListing() const
{
    if(!isListing()){
        throwNotListingException();
    }
    return static_cast<const Listing*>(this);
}


Listing* ValueNode::toListing()
{
    if(!isListing()){
        throwNotListingException();
    }
    return static_cast<Listing*>(this);
}


void ValueNode::throwException(const std::string& message) const
{
    Exception ex;
    ex.setPosition(line(), column());
    ex.setMessage(message);
    throw ex;
}


void ValueNode::throwNotScalarException() const
{
    NotScalarException ex;
    ex.setPosition(line(), column());
    ex.setMessage(formatR(_("A {} value must be a scalar value"), getTypeName(typeBits)));
    throw ex;
}


void ValueNode::throwNotMappingException() const
{
    NotMappingException ex;
    ex.setPosition(line(), column());
    ex.setMessage(_("The value is not a mapping"));
    throw ex;
}


void ValueNode::throwNotListingException() const
{
    NotListingException ex;
    ex.setPosition(line(), column());
    ex.setMessage(_("The value is not a listing"));
    throw ex;
}


ScalarNode::ScalarNode(const std::string& value, StringStyle stringStyle)
    : stringValue_(value),
      stringStyle_(stringStyle)
{
    typeBits = SCALAR;
    line_ = -1;
    column_ = -1;
}


ScalarNode::ScalarNode(const char* text, size_t length)
    : stringValue_(text, length)
{
    typeBits = SCALAR;
    stringStyle_ = PLAIN_STRING;
}


ScalarNode::ScalarNode(const char* text, size_t length, StringStyle stringStyle)
    : stringValue_(text, length),
      stringStyle_(stringStyle)
{
    typeBits = SCALAR;
    line_ = -1;
    column_ = -1;
}


ScalarNode::ScalarNode(int value)
    : stringValue_(std::to_string(value))
{
    typeBits = SCALAR;
    line_ = -1;
    column_ = -1;
    stringStyle_ = PLAIN_STRING;
}


ScalarNode::ScalarNode(double value, const char* floatingNumberFormat_)
    : stringValue_(std::to_string(value))
{
    char buf[32];
    int n = snprintf(buf, 32, floatingNumberFormat_ ? floatingNumberFormat_ : defaultFloatingNumberFormat, value);
    stringValue_.assign(buf, n);

    typeBits = SCALAR;
    line_ = -1;
    column_ = -1;
    stringStyle_ = PLAIN_STRING;
}


ScalarNode::ScalarNode(const ScalarNode& org)
    : ValueNode(org),
      stringValue_(org.stringValue_),
      stringStyle_(org.stringStyle_)
{

}


ValueNode* ScalarNode::clone() const
{
    return new ScalarNode(*this);
}


Mapping::Mapping()
{
    typeBits = MAPPING;
    line_ = -1;
    column_ = -1;
    mode = READ_MODE;
    indexCounter = 0;
    keyStringStyle_ = PLAIN_STRING;
    isFlowStyle_ = false;
    floatingNumberFormat_ = defaultFloatingNumberFormat;
}


Mapping::Mapping(int line, int column)
{
    typeBits = MAPPING;
    line_ = line;
    column_ = column;
    mode = READ_MODE;
    indexCounter = 0;
    isFlowStyle_ = false;
    floatingNumberFormat_ = defaultFloatingNumberFormat;
}


Mapping::Mapping(const Mapping& org)
    : ValueNode(org),
      values(org.values),
      mode(org.mode),
      floatingNumberFormat_(org.floatingNumberFormat_),
      isFlowStyle_(org.isFlowStyle_),
      keyStringStyle_(org.keyStringStyle_)
{
    
}


ValueNode* Mapping::clone() const
{
    return new Mapping(*this);
}


Mapping* Mapping::cloneMapping() const
{
    return new Mapping(*this);
}


Mapping::~Mapping()
{
    clear();
}


void Mapping::clear()
{
    values.clear();
    indexCounter = 0;
}


void Mapping::setFloatingNumberFormat(const char* format)
{
    floatingNumberFormat_ = format;
}


void Mapping::setKeyQuoteStyle(StringStyle style)
{
    keyStringStyle_ = style;
}


ValueNode* Mapping::find(const std::string& key) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    auto p = values.find(key);
    if(p != values.end()){
        return p->second.get();
    } else {
        return invalidNode.get();
    }
}


ValueNode* Mapping::find(std::initializer_list<const char*> keys) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    for(auto& key : keys){
        auto p = values.find(key);
        if(p != values.end()){
            return p->second.get();
        }
    }
    return invalidNode.get();
}


Mapping* Mapping::findMapping(const std::string& key) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    auto p = values.find(key);
    if(p != values.end()){
        ValueNode* node = p->second.get();
        if(node->isMapping()){
            return static_cast<Mapping*>(node);
        }
    }
    return invalidMapping.get();
}


Mapping* Mapping::findMapping(std::initializer_list<const char*> keys) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    for(auto& key : keys){
        auto p = values.find(key);
        if(p != values.end()){
            ValueNode* node = p->second.get();
            if(node->isMapping()){
                return static_cast<Mapping*>(node);
            }
        }
    }
    return invalidMapping.get();
}


Listing* Mapping::findListing(const std::string& key) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    auto  p = values.find(key);
    if(p != values.end()){
        ValueNode* node = p->second.get();
        if(node->isListing()){
            return static_cast<Listing*>(node);
        }
    }
    return invalidListing.get();
}


Listing* Mapping::findListing(std::initializer_list<const char*> keys) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    for(auto& key : keys){
        auto  p = values.find(key);
        if(p != values.end()){
            ValueNode* node = p->second.get();
            if(node->isListing()){
                return static_cast<Listing*>(node);
            }
        }
    }
    return invalidListing.get();
}


ValueNodePtr Mapping::extract(const std::string& key)
{
    if(!isValid()){
        throwNotMappingException();
    }
    auto p = values.find(key);
    if(p != values.end()){
        ValueNodePtr value = p->second;
        values.erase(p);
        return value;
    }
    return nullptr;
}


ValueNodePtr Mapping::extract(std::initializer_list<const char*> keys)
{
    if(!isValid()){
        throwNotMappingException();
    }
    for(auto& key : keys){
        auto p = values.find(key);
        if(p != values.end()){
            ValueNodePtr node = p->second;
            values.erase(p);
            return node;
        }
    }
    return nullptr;
}    


bool Mapping::extract(const std::string& key, double& out_value)
{
    ValueNodePtr node = extract(key);
    if(node){
        out_value = node->toDouble();
        return true;
    }
    return false;
}


bool Mapping::extract(const std::string& key, std::string& out_value)
{
    ValueNodePtr node = extract(key);
    if(node){
        out_value = node->toString();
        return true;
    }
    return false;
}


ValueNode& Mapping::get(const std::string& key) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    auto p = values.find(key);
    if(p == values.end()){
        throwKeyNotFoundException(key);
    }
    return *p->second;
}


ValueNode& Mapping::get(std::initializer_list<const char*> keys) const
{
    if(!isValid()){
        throwNotMappingException();
    }
    ValueNode* node = nullptr;
    for(auto& key : keys){
        auto p = values.find(key);
        if(p != values.end()){
            node = &(*p->second);
            break;
        }
    }
    if(!node){
        throwKeyNotFoundException(*keys.begin());
    }
    return *node;
}
    
        
void Mapping::throwKeyNotFoundException(const std::string& key) const
{
    KeyNotFoundException ex;
    ex.setPosition(line(), column());
    ex.setKey(key);
    ex.setMessage(formatR(_("Key \"{}\" is not found in the mapping"), key));
    throw ex;
}


inline void Mapping::insertSub(const std::string& key, ValueNode* node)
{
    if(key.empty()){
        EmptyKeyException ex;
        throw ex;
    }
    //values.insert(make_pair(key, node));
    values[key] = node;
    node->indexInMapping_ = indexCounter++;
}


void Mapping::insert(const std::string& key, ValueNode* node)
{
    if(!isValid()){
        throwNotMappingException();
    }
    if(!node){
        throwException(_("A node to insert into a Mapping is a null node"));
    }
    const string uKey(key);
    insertSub(uKey, node);
}


void Mapping::insert(const Mapping* other, bool doArrangeElementIndices)
{
    if(!isValid()){
        throwNotMappingException();
    }

    if(other->empty()){
        return;
    }

    if(doArrangeElementIndices){
        int minIndexInOther = std::numeric_limits<int>::max();
        int maxIndexInOther = 0;
        for(auto& kv : *other){
            int index = kv.second->indexInMapping_;
            if(index < minIndexInOther){
                minIndexInOther = index;
            }
            if(index > maxIndexInOther){
                maxIndexInOther = index;
            }
        }
        int offset = 0;
        if(minIndexInOther < indexCounter){
            offset = indexCounter - minIndexInOther;
            for(auto& kv : *other){
                kv.second->indexInMapping_ += offset;
            }
            maxIndexInOther += offset;
            if(maxIndexInOther > other->indexCounter){
                other->indexCounter = maxIndexInOther;
            }
        }
        indexCounter = maxIndexInOther + 1;
    }

    values.insert(other->values.begin(), other->values.end());
}


Mapping* Mapping::openMapping_(const std::string& key, bool doOverwrite)
{
    if(!isValid()){
        throwNotMappingException();
    }

    Mapping* mapping = nullptr;
    const string uKey(key);
    iterator p = values.find(uKey);
    if(p != values.end()){
        ValueNode* node = p->second.get();
        if(!node->isMapping()){
            values.erase(p);
        } else {
            mapping = static_cast<Mapping*>(node);
            if(doOverwrite){
                mapping->clear();
            }
            mapping->indexInMapping_ = indexCounter++;
        }
    }

    if(!mapping){
        mapping = new Mapping;
        mapping->floatingNumberFormat_ = floatingNumberFormat_;
        insertSub(uKey, mapping);
    }

    return mapping;
}


Mapping* Mapping::openFlowStyleMapping_(const std::string& key, bool doOverwrite)
{
    Mapping* m = openMapping_(key, doOverwrite);
    m->setFlowStyle(true);
    return m;
}


Listing* Mapping::openListing_(const std::string& key, bool doOverwrite)
{
    if(!isValid()){
        throwNotMappingException();
    }

    Listing* sequence = nullptr;
    const string uKey(key);
    iterator p = values.find(uKey);
    if(p != values.end()){
        ValueNode* node = p->second.get();
        if(!node->isListing()){
            values.erase(p);
        } else {
            sequence = static_cast<Listing*>(node);
            if(doOverwrite){
                sequence->clear();
            }
            sequence->indexInMapping_ = indexCounter++;
        }
    }

    if(!sequence){
        sequence = new Listing;
        sequence->floatingNumberFormat_ = floatingNumberFormat_;
        insertSub(uKey, sequence);
    }

    return sequence;
}


Listing* Mapping::openFlowStyleListing_(const std::string& key, bool doOverwrite)
{
    Listing* s = openListing_(key, doOverwrite);
    s->setFlowStyle(true);
    return s;
}


bool Mapping::remove(const std::string& key)
{
    return (values.erase(key) > 0);
}


bool Mapping::read(const std::string &key, std::string &out_value) const
{
    ValueNode* node = find(key);
    if(node->isValid()){
        return node->read(out_value);
    }
    return false;
}


bool Mapping::read(const std::string &key, bool &out_value) const
{
    ValueNode* node = find(key);
    if(node->isValid()){
        return node->read(out_value);
    }
    return false;
}


bool Mapping::read(const std::string &key, int &out_value) const
{
    ValueNode* node = find(key);
    if(node->isValid()){
        return node->read(out_value);
    }
    return false;
}


bool Mapping::read(const std::string &key, double &out_value) const
{
    ValueNode* node = find(key);
    if(node->isValid()){
        return node->read(out_value);
    }
    return false;
}


bool Mapping::read(const std::string &key, float &out_value) const
{
    ValueNode* node = find(key);
    if(node->isValid()){
        return node->read(out_value);
    }
    return false;
}

template<class T>
static bool readAngle_(const Mapping* mapping, const std::string& key, T& out_angle, const ValueNode* unitAttrNode)
{
    if(mapping->read(key, out_angle)){
        bool isDegree = true;
        if(unitAttrNode){
            isDegree = !unitAttrNode->isForcedRadianMode();
        } else {
            isDegree = !mapping->isForcedRadianMode();
        }
        if(isDegree){
            out_angle = radian(out_angle);
        }
        return true;
    }
    return false;
}


bool Mapping::readAngle(const std::string& key, double& out_angle, const ValueNode* unitAttrNode) const
{
    return readAngle_(this, key, out_angle, unitAttrNode);
}


bool Mapping::readAngle(const std::string& key, float& out_angle, const ValueNode* unitAttrNode) const
{
    return readAngle_(this, key, out_angle, unitAttrNode);
}


template<class T>
static bool readAngle_
(const Mapping* mapping, std::initializer_list<const char*> keys, T& out_angle, const ValueNode* unitAttrNode)
{
    for(auto& key : keys){
        if(readAngle_(mapping, key, out_angle, unitAttrNode)){
            return true;
        }
    }
    return false;
}


bool Mapping::readAngle
(std::initializer_list<const char*> keys, double& out_angle, const ValueNode* unitAttrNode) const
{
    return readAngle_(this, keys, out_angle, unitAttrNode);
}


bool Mapping::readAngle
(std::initializer_list<const char*> keys, float& out_angle, const ValueNode* unitAttrNode) const
{
    return readAngle_(this, keys, out_angle, unitAttrNode);
}


void Mapping::write(const std::string &key, const std::string& value, StringStyle stringStyle)
{
    iterator p = values.find(key);
    if(p == values.end()){
        insertSub(key, new ScalarNode(value, stringStyle));
    } else {
        ValueNode* node = p->second.get();
        if(node->isScalar()){
            ScalarNode* scalar = static_cast<ScalarNode*>(node);
            scalar->stringValue_ = value;
            scalar->stringStyle_ = stringStyle;
            scalar->indexInMapping_ = indexCounter++;
        } else {
            throwNotScalarException();
        }
    }
}


void Mapping::writeSub(const std::string &key, const char* text, size_t length, StringStyle stringStyle)
{
    iterator p = values.find(key);
    if(p == values.end()){
        insertSub(key, new ScalarNode(text, length, stringStyle));
    } else {
        ValueNode* node = p->second.get();
        if(node->isScalar()){
            ScalarNode* scalar = static_cast<ScalarNode*>(node);
            scalar->stringValue_ = string(text, length);
            scalar->stringStyle_ = stringStyle;
            scalar->indexInMapping_ = indexCounter++;
        } else {
            throwNotScalarException();
        }
    }
}



void Mapping::write(const std::string &key, bool value)
{
    if(value){
        writeSub(key, "true", 4, PLAIN_STRING);
    } else {
        writeSub(key, "false", 5, PLAIN_STRING);
    }
}


void Mapping::write(const std::string &key, int value)
{
    char buf[32];
    int n = snprintf(buf, 32, "%d", value);
    writeSub(key, buf, n, PLAIN_STRING);
}


void Mapping::write(const std::string &key, double value)
{
    char buf[32];
    int n = snprintf(buf, 32, floatingNumberFormat_, value);
    writeSub(key, buf, n, PLAIN_STRING);
}


void Mapping::writePath(const std::string &key, const std::string& value)
{
    auto path = toUTF8(stdx::filesystem::path(fromUTF8(value)).generic_string());
    write(key, path, DOUBLE_QUOTED);
}


size_t Mapping::getContentHash() const
{
    size_t seed = 0;
    std::hash<string> hasher;
    for(auto& kv : *this){
        auto& node = kv.second;
        if(node->isScalar()){
            hash_combine(seed, hasher(node->toString()));
        } else if(node->isMapping()){
            hash_combine(seed, node->toMapping()->getContentHash());
        } else if(node->isListing()){
            hash_combine(seed, node->toListing()->getContentHash());
        }
    }
    return seed;
}
        
         
Listing::Listing()
{
    typeBits = LISTING;
    line_ = -1;
    column_ = -1;
    floatingNumberFormat_ = defaultFloatingNumberFormat;
    isFlowStyle_ = false;
    doInsertLFBeforeNextElement = false;
}


Listing::Listing(int size)
    : values(size)
{
    typeBits = LISTING;
    line_ = -1;
    column_ = -1;
    floatingNumberFormat_ = defaultFloatingNumberFormat;
    isFlowStyle_ = false;
    doInsertLFBeforeNextElement = false;
}


Listing::Listing(int line, int column)
{
    typeBits = LISTING;
    line_ = line;
    column_ = column;
    floatingNumberFormat_ = defaultFloatingNumberFormat;
    isFlowStyle_ = false;
    doInsertLFBeforeNextElement = false;
}


Listing::Listing(int line, int column, int reservedSize)
    : values(reservedSize)
{
    typeBits = LISTING;
    line_ = line;
    column_ = column;
    values.resize(0);
    floatingNumberFormat_ = defaultFloatingNumberFormat;
    isFlowStyle_ = false;
    doInsertLFBeforeNextElement = false;
}


Listing::Listing(const Listing& org)
    : ValueNode(org),
      values(org.values),
      floatingNumberFormat_(org.floatingNumberFormat_),
      isFlowStyle_(org.isFlowStyle_),
      doInsertLFBeforeNextElement(org.doInsertLFBeforeNextElement)
{

}


ValueNode* Listing::clone() const
{
    return new Listing(*this);
}


Listing::~Listing()
{
    clear();
}


void Listing::clear()
{
    values.clear();
}


void Listing::reserve(int size)
{
    values.reserve(size);
}


void Listing::setFloatingNumberFormat(const char* format)
{
    floatingNumberFormat_ = format;
}


void Listing::insertLF(int maxColumns, int numValues)
{
    if(values.empty()){
        if(numValues > 0 && numValues > maxColumns){
            doInsertLFBeforeNextElement = true;
        }
    } else if((values.size() % maxColumns) == 0){
        values.back()->typeBits |= (TypeBit)APPEND_LF;
    }
}


void Listing::appendLF()
{
    if(values.empty()){
        doInsertLFBeforeNextElement = true;
    } else {
        values.back()->typeBits |= APPEND_LF;
    }
}


Mapping* Listing::newMapping()
{
    Mapping* mapping = new Mapping;
    mapping->floatingNumberFormat_ = floatingNumberFormat_;
    append(mapping);
    return mapping;
}


void Listing::append(int value)
{
    char buf[32];
    int n = snprintf(buf, 32, "%d", value);
    ScalarNode* node = new ScalarNode(buf, n, PLAIN_STRING);
    if(doInsertLFBeforeNextElement){
        node->typeBits |= INSERT_LF;
        doInsertLFBeforeNextElement = false;
    }
    values.push_back(node);
}


void Listing::write(int i, int value)
{
    char buf[32];
    int n = snprintf(buf, 32, "%d", value);
    values[i] = new ScalarNode(buf, n, PLAIN_STRING);
}


/*
  void Listing::append(size_t value)
  {
  char buf[32];
  int n = snprintf(buf, 32, "%zd", value);
  values.push_back(new ScalarNode(buf, n, PLAIN_STRING));
  }
*/


void Listing::append(double value)
{
    char buf[32];
    int n = snprintf(buf, 32, floatingNumberFormat_, value);
    ScalarNode* node = new ScalarNode(buf, n, PLAIN_STRING);
    if(doInsertLFBeforeNextElement){
        node->typeBits |= INSERT_LF;
        doInsertLFBeforeNextElement = false;
    }
    values.push_back(node);
}


void Listing::append(const std::string& value, StringStyle stringStyle)
{
    ScalarNode* node = new ScalarNode(value, stringStyle);
    if(doInsertLFBeforeNextElement){
        node->typeBits |= INSERT_LF;
        doInsertLFBeforeNextElement = false;
    }
    values.push_back(node);
}


void Listing::insert(int index, ValueNode* node)
{
    if(index >= 0){
        if(index > static_cast<int>(values.size())){
            index = values.size();
        }
        values.insert(values.begin() + index, node);
    }
}


void Listing::write(int i, const std::string& value, StringStyle stringStyle)
{
    values[i] = new ScalarNode(value, stringStyle);
}


size_t Listing::getContentHash() const
{
    size_t seed = 0;
    std::hash<string> hasher;
    for(auto& node : *this){
        if(node->isScalar()){
            hash_combine(seed, hasher(node->toString()));
        } else if(node->isMapping()){
            hash_combine(seed, node->toMapping()->getContentHash());
        } else if(node->isListing()){
            hash_combine(seed, node->toListing()->getContentHash());
        }
    }
    return seed;
}
