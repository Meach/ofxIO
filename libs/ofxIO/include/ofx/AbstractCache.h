//
// AbstractCache.h
//
// $Id: //poco/1.4/Foundation/include/Poco/AbstractCache.h#1 $
//
// Library: Foundation
// Package: Cache
// Module:  AbstractCache
//
// Definition of the AbstractCache class.
//
// Copyright (c) 2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#pragma once


#include "Poco/KeyValueArgs.h"
#include "Poco/ValidArgs.h" 
#include "Poco/Mutex.h"
#include "Poco/Exception.h"
#include "Poco/BasicEvent.h"
#include "Poco/EventArgs.h"
#include "Poco/Delegate.h"
#include "Poco/SharedPtr.h"
#include <map>
#include <set>
#include <cstddef>
#include "ofTypes.h"


namespace ofx {


/// \brief An AbstractCache is the interface of all caches.
///
/// A temporary replacement for Poco::AbstractCache that uses std::shared_ptr.
template <
    class TKey,
    class TValue,
    class TStrategy,
    class TMutex = Poco::FastMutex,
    class TEventMutex = Poco::FastMutex
> 
class AbstractCache
{
public:
	Poco::BasicEvent<const Poco::KeyValueArgs<TKey, TValue >, TEventMutex > Add;
	Poco::BasicEvent<const Poco::KeyValueArgs<TKey, TValue >, TEventMutex > Update;
	Poco::BasicEvent<const TKey, TEventMutex> Remove;
	Poco::BasicEvent<const TKey, TEventMutex> Get;
	Poco::BasicEvent<const Poco::EventArgs, TEventMutex> Clear;

	typedef std::map<TKey, std::shared_ptr<TValue> > DataHolder;
	typedef typename DataHolder::iterator       Iterator;
	typedef typename DataHolder::const_iterator ConstIterator;
	typedef std::set<TKey>                      KeySet;

	AbstractCache()
	{
		initialize();
	}

	AbstractCache(const TStrategy& strat): _strategy(strat)
	{
		initialize();
	}

	virtual ~AbstractCache()
	{
		uninitialize();
	}

    /// \brief Adds the key value pair to the cache.
    ///
    /// If for the key already an entry exists, it will be overwritten.
	void add(const TKey& key, const TValue& val)
	{
		typename TMutex::ScopedLock lock(_mutex);
		doAdd(key, val);
	}

    /// \brief Adds the key value pair to the cache. Note that adding a NULL SharedPtr will fail!
    ///
    /// If for the key already an entry exists, it will be overwritten.
    /// The difference to add is that no remove or add events are thrown in this case,
    /// just a simply silent update is performed
    /// If the key doesnot exist the behavior is equal to add, ie. an add event is thrown
	void update(const TKey& key, const TValue& val)
	{
		typename TMutex::ScopedLock lock(_mutex);
		doUpdate(key, val);
	}

    /// \brief Adds the key value pair to the cache. Note that adding a NULL SharedPtr will fail!
    ///
    /// If for the key already an entry exists, it will be overwritten, ie. first a remove event
    /// is thrown, then a add event
	void add(const TKey& key, std::shared_ptr<TValue> val)
	{
		typename TMutex::ScopedLock lock(_mutex);
		doAdd(key, val);
	}

    /// \brief Adds the key value pair to the cache. Note that adding a NULL SharedPtr will fail!
    ///
    /// If for the key already an entry exists, it will be overwritten.
    /// The difference to add is that no remove or add events are thrown in this case,
    /// just an Update is thrown
    /// If the key doesnot exist the behavior is equal to add, ie. an add event is thrown
	void update(const TKey& key, std::shared_ptr<TValue> val)
	{
		typename TMutex::ScopedLock lock(_mutex);
		doUpdate(key, val);
	}

    /// \brief Removes an entry from the cache. If the entry is not found,
    /// the remove is ignored.
	void remove(const TKey& key)
	{
		typename TMutex::ScopedLock lock(_mutex);
		Iterator it = _data.find(key);
		doRemove(it);
	}

    /// \returns true if the cache contains a value for the key.
	bool has(const TKey& key) const
	{
		typename TMutex::ScopedLock lock(_mutex);
		return doHas(key);
	}

    /// \returns a SharedPtr of the value. The SharedPointer will remain valid
    /// even when cache replacement removes the element.
    /// If for the key no value exists, an empty SharedPtr is returned.
	std::shared_ptr<TValue> get(const TKey& key)
	{
		typename TMutex::ScopedLock lock(_mutex);
		return doGet (key);
	}

    /// Removes all elements from the cache.
	void clear()
	{
		typename TMutex::ScopedLock lock(_mutex);
		doClear();
	}

    /// Returns the number of cached elements
	std::size_t size()
	{
		typename TMutex::ScopedLock lock(_mutex);
		doReplace();
		return _data.size();
	}

    /// Forces cache replacement. Note that Poco's cache strategy use for efficiency reason no background thread
    /// which periodically triggers cache replacement. Cache Replacement is only started when the cache is modified
    /// from outside, i.e. add is called, or when a user tries to access an cache element via get.
    /// In some cases, i.e. expire based caching where for a long time no access to the cache happens,
    /// it might be desirable to be able to trigger cache replacement manually.
	void forceReplace()
	{
		typename TMutex::ScopedLock lock(_mutex);
		doReplace();
	}

    /// Returns a copy of all keys stored in the cache
	std::set<TKey> getAllKeys()
	{
		typename TMutex::ScopedLock lock(_mutex);
		doReplace();
		ConstIterator it = _data.begin();
		ConstIterator itEnd = _data.end();
		std::set<TKey> result;
		for (; it != itEnd; ++it)
			result.insert(it->first);

		return result;
	}

protected:
	mutable Poco::BasicEvent<Poco::ValidArgs<TKey> > IsValid;
	mutable Poco::BasicEvent<KeySet>           Replace;

    /// Sets up event registration.
	void initialize()
	{
		Add		+= Poco::Delegate<TStrategy, const Poco::KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onAdd);
		Update	+= Poco::Delegate<TStrategy, const Poco::KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onUpdate);
		Remove	+= Poco::Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onRemove);
		Get		+= Poco::Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onGet);
		Clear	+= Poco::Delegate<TStrategy, const Poco::EventArgs>(&_strategy, &TStrategy::onClear);
		IsValid	+= Poco::Delegate<TStrategy, Poco::ValidArgs<TKey> >(&_strategy, &TStrategy::onIsValid);
		Replace	+= Poco::Delegate<TStrategy, KeySet>(&_strategy, &TStrategy::onReplace);
	}

    /// Reverts event registration.
	void uninitialize()
	{
		Add		-= Poco::Delegate<TStrategy, const Poco::KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onAdd );
		Update	-= Poco::Delegate<TStrategy, const Poco::KeyValueArgs<TKey, TValue> >(&_strategy, &TStrategy::onUpdate);
		Remove	-= Poco::Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onRemove);
		Get		-= Poco::Delegate<TStrategy, const TKey>(&_strategy, &TStrategy::onGet);
		Clear	-= Poco::Delegate<TStrategy, const Poco::EventArgs>(&_strategy, &TStrategy::onClear);
		IsValid	-= Poco::Delegate<TStrategy, Poco::ValidArgs<TKey> >(&_strategy, &TStrategy::onIsValid);
		Replace	-= Poco::Delegate<TStrategy, KeySet>(&_strategy, &TStrategy::onReplace);
	}

    /// Adds the key value pair to the cache.
    /// If for the key already an entry exists, it will be overwritten.
	void doAdd(const TKey& key, const TValue& val)
	{
		Iterator it = _data.find(key);
		doRemove(it);

		Poco::KeyValueArgs<TKey, TValue> args(key, val);
		Add.notify(this, args);
		_data.insert(std::make_pair(key, std::make_shared<TValue>(val)));
		
		doReplace();
	}

    /// Adds the key value pair to the cache.
    /// If for the key already an entry exists, it will be overwritten.
	void doAdd(const TKey& key, std::shared_ptr<TValue>& val)
	{
		Iterator it = _data.find(key);
		doRemove(it);

        Poco::KeyValueArgs<TKey, TValue> args(key, *val);
		Add.notify(this, args);
		_data.insert(std::make_pair(key, val));
		
		doReplace();
	}

    /// Adds the key value pair to the cache.
    /// If for the key already an entry exists, it will be overwritten.
	void doUpdate(const TKey& key, const TValue& val)
	{
        Poco::KeyValueArgs<TKey, TValue> args(key, val);
		Iterator it = _data.find(key);
		if (it == _data.end())
		{
			Add.notify(this, args);
			_data.insert(std::make_pair(key, std::make_shared<TValue>(val)));
		}
		else
		{
			Update.notify(this, args);
			it->second = std::make_shared<TValue>(val);
		}
		
		doReplace();
	}

    /// Adds the key value pair to the cache.
    /// If for the key already an entry exists, it will be overwritten.
	void doUpdate(const TKey& key, std::shared_ptr<TValue>& val)
	{
        Poco::KeyValueArgs<TKey, TValue> args(key, *val);
		Iterator it = _data.find(key);
		if (it == _data.end())
		{
			Add.notify(this, args);
			_data.insert(std::make_pair(key, val));
		}
		else
		{
			Update.notify(this, args);
			it->second = val;
		}
		
		doReplace();
	}

    /// Removes an entry from the cache. If the entry is not found
    /// the remove is ignored.
	void doRemove(Iterator it)
	{
		if (it != _data.end())
		{
			Remove.notify(this, it->first);
			_data.erase(it);
		}
	}

    /// Returns true if the cache contains a value for the key
	bool doHas(const TKey& key) const
	{
		// ask the strategy if the key is valid
		ConstIterator it = _data.find(key);
		bool result = false;

		if (it != _data.end())
		{
            Poco::ValidArgs<TKey> args(key);
			IsValid.notify(this, args);
			result = args.isValid();
		}

		return result;
	}

    /// Returns a SharedPtr of the cache entry, returns 0 if for
    /// the key no value was found
	std::shared_ptr<TValue> doGet(const TKey& key)
	{
		Iterator it = _data.find(key);
        std::shared_ptr<TValue> result;

		if (it != _data.end())
		{	
			// inform all strategies that a read-access to an element happens
			Get.notify(this, key);
			// ask all strategies if the key is valid
			Poco::ValidArgs<TKey> args(key);
			IsValid.notify(this, args);

			if (!args.isValid())
			{
				doRemove(it);
			}
			else
			{
				result = it->second;
			}
		}

		return result;
	}

	void doClear()
	{
		static Poco::EventArgs _emptyArgs;
		Clear.notify(this, _emptyArgs);
		_data.clear();
	}

	void doReplace()
	{
		std::set<TKey> delMe;
		Replace.notify(this, delMe);
		// delMe contains the to be removed elements
		typename std::set<TKey>::const_iterator it    = delMe.begin();
		typename std::set<TKey>::const_iterator endIt = delMe.end();

		for (; it != endIt; ++it)
		{
			Iterator itH = _data.find(*it);
			doRemove(itH);
		}
	}

	TStrategy          _strategy;
	mutable DataHolder _data;
	mutable TMutex  _mutex;

private:
	AbstractCache(const AbstractCache& aCache);
	AbstractCache& operator = (const AbstractCache& aCache);
};


} // namespace ofx
