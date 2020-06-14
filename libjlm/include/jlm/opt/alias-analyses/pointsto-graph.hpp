/*
 * Copyright 2020 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#ifndef JLM_OPT_ALIAS_ANALYSES_POINTSTO_GRAPH_HPP
#define JLM_OPT_ALIAS_ANALYSES_POINTSTO_GRAPH_HPP

#include <jlm/common.hpp>

#include <jive/rvsdg/node.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace jive {
	class node;
	class output;
}

namespace jlm {

class rvsdg_module;

namespace aa {

/** /brief Points-to graph
*
* FIXME: write documentation
*/
class ptg final {
	class constiterator;
	class iterator;

public:
	class allocator;
	class edge;
	class memnode;
	class node;
	class regnode;
	class unknown;

	using memnodemap = std::unordered_map<const jive::node*, std::unique_ptr<ptg::memnode>>;
	using regnodemap = std::unordered_map<const jive::output*, std::unique_ptr<ptg::regnode>>;

private:
	ptg();

	ptg(const jlm::aa::ptg&) = delete;

	ptg(jlm::aa::ptg&&) = delete;

	jlm::aa::ptg &
	operator=(const jlm::aa::ptg&) = delete;

	jlm::aa::ptg &
	operator=(jlm::aa::ptg&&) = delete;

public:
	iterator
	begin();

	constiterator
	begin() const;

	iterator
	end();

	constiterator
	end() const;

	/*
		FIXME: rename to nallocations or something
	*/
	size_t
	nmemnodes() const noexcept
	{
		return memnodes_.size();
	}

	size_t
	nregnodes() const noexcept
	{
		return regnodes_.size();
	}

	size_t
	nnodes() const noexcept
	{
		return nmemnodes() + nregnodes();
	}

	jlm::aa::ptg::unknown &
	memunknown() const noexcept
	{
		return *memunknown_;
	}

	/*
		FIXME: I would like to call this function memnode() or node().
	*/
	const ptg::memnode &
	find(const jive::node * node) const
	{
		auto it = memnodes_.find(node);
		if (it == memnodes_.end())
			throw error("Cannot find node in points-to graph.");

		return *it->second;
	}

	/*
		FIXME: I would like to call this function regnode() or node().
	*/
	const ptg::regnode &
	find(const jive::output * output) const
	{
		auto it = regnodes_.find(output);
		if (it == regnodes_.end())
			throw error("Cannot find node in points-to graph.");

		return *it->second;
	}

	/*
		FIXME: change return value to ptg::node &
	*/
	jlm::aa::ptg::node *
	add(std::unique_ptr<ptg::allocator> node);

	/*
		FIXME: change return value to ptg::node &
	*/
	jlm::aa::ptg::node *
	add(std::unique_ptr<ptg::regnode> node);

	/** FIXME: write documentation
	*/
	static void
	encode(const jlm::aa::ptg & ptg, rvsdg_module & module);

	static std::string
	to_dot(const jlm::aa::ptg & ptg);

	static std::unique_ptr<ptg>
	create()
	{
		return std::unique_ptr<jlm::aa::ptg>(new ptg());
	}

private:
	memnodemap memnodes_;
	regnodemap regnodes_;
	std::unique_ptr<jlm::aa::ptg::unknown> memunknown_;
};


/** \brief Points-to graph node
*
* FIXME: write documentation
*/
class ptg::node {
	class constiterator;
	class iterator;

public:
	virtual
	~node();

	/*
		FIXME: change to ptg &
	*/
	node(jlm::aa::ptg * ptg)
	: ptg_(ptg)
	{}

	node(const node&) = delete;

	node(node&&) = delete;

	node&
	operator=(const node&) = delete;

	node&
	operator=(node&&) = delete;

	iterator
	begin();

	constiterator
	begin() const;

	iterator
	end();

	constiterator
	end() const;

	/*
		FIXME: change to ptg &
	*/
	jlm::aa::ptg *
	ptg() const noexcept
	{
		return ptg_;
	}

	size_t
	ntargets() const noexcept
	{
		return targets_.size();
	}

	virtual std::string
	debug_string() const = 0;

	/*
		FIXME: change to ptg::node &
		FIXME: I believe that this can only be a memnode. If so, make it explicit in the type.
	*/
	void
	add_edge(ptg::node * target);

private:
	jlm::aa::ptg * ptg_;
	std::unordered_set<ptg::node*> targets_;
};


/** \brief Points-to graph register node
*
* FIXME: write documentation
*/
class ptg::regnode final : public ptg::node {
public:
	~regnode() override;

private:
	regnode(
		jlm::aa::ptg * ptg,
		const jive::output * output)
	: node(ptg)
	, output_(output)
	{}

public:
	const jive::output *
	output() const noexcept
	{
		return output_;
	}

	virtual std::string
	debug_string() const override;

	/**
		FIXME: write documentation
	*/
	static std::vector<const ptg::allocator*>
	allocators(const ptg::regnode & node);

	static ptg::regnode *
	create(jlm::aa::ptg * ptg, const jive::output * output)
	{
		auto node = std::unique_ptr<ptg::regnode>(new regnode(ptg, output));
		return static_cast<regnode*>(ptg->add(std::move(node)));
	}

private:
	const jive::output * output_;
};


/** \brief Points-to graph memory node
*
* FIXME: write documentation
*
* FIXME: Add final and convert protected to private after unknown inheritance is resolved.
*/
class ptg::memnode : public ptg::node {
public:
	~memnode() override;

protected:
	memnode(jlm::aa::ptg * ptg)
	: node(ptg)
	{}
};


/**
* FIXME: write documentation
*/
class ptg::allocator final : public ptg::memnode {
public:
	~allocator() override;

private:
	allocator(
		jlm::aa::ptg * ptg
	, const jive::node * node)
	: memnode(ptg)
	, node_(node)
	{}

public:
	const jive::node *
	node() const noexcept
	{
		return node_;
	}

	virtual std::string
	debug_string() const override;

	static jlm::aa::ptg::allocator *
	create(
		jlm::aa::ptg * ptg
	, const jive::node * node)
	{
		auto n = std::unique_ptr<ptg::allocator>(new allocator(ptg, node));
		return static_cast<jlm::aa::ptg::allocator*>(ptg->add(std::move(n)));
	}

private:
	const jive::node * node_;
};


/**
*
* FIXME: write documentation
*/
class ptg::unknown final : public ptg::memnode {
	friend jlm::aa::ptg;

public:
	~unknown() override;

private:
	unknown(jlm::aa::ptg * ptg)
	: memnode(ptg)
	{}

	virtual std::string
	debug_string() const override;
};


/** \brief Points-to graph node iterator
*/
class ptg::iterator final : public std::iterator<std::forward_iterator_tag,
	ptg::node*, ptrdiff_t> {

	friend jlm::aa::ptg;

	iterator(
		const memnodemap::iterator & mnit
	, const memnodemap::iterator & mnend
	, const regnodemap::iterator & rnit)
	: mnit_(mnit)
	, rnit_(rnit)
	, mnend_(mnend)
	{}

public:
	ptg::node *
	node() const noexcept
	{
		if (mnit_ != mnend_)
			return mnit_->second.get();

		return rnit_->second.get();
	}	

	ptg::node &
	operator*() const
	{
		JLM_DEBUG_ASSERT(node() != nullptr);
		return *node();
	}

	ptg::node *
	operator->() const
	{
		return node();
	}	

	iterator &
	operator++()
	{
		if (mnit_ != mnend_)
			++mnit_;
		else
			++rnit_;

		return *this;
	}

	iterator
	operator++(int)
	{
		iterator tmp = *this;
		++*this;
		return tmp;
	}

	bool
	operator==(const iterator & other) const
	{
		return mnit_ == other.mnit_
		    && rnit_ == other.rnit_;
	}

	bool
	operator!=(const iterator & other) const
	{
		return !operator==(other);
	}

private:
	memnodemap::iterator mnit_;
	regnodemap::iterator rnit_;
	memnodemap::iterator mnend_;
};

/** \brief Points-to graph node const iterator
*/
class ptg::constiterator final : public std::iterator<std::forward_iterator_tag,
	const ptg::node*, ptrdiff_t> {

	friend jlm::aa::ptg;

	constiterator(
	  const memnodemap::const_iterator & mnit
	, const memnodemap::const_iterator & mnend
	, const regnodemap::const_iterator & rnit)
	: mnit_(mnit)
	, rnit_(rnit)
	, mnend_(mnend)
	{}

public:
	const ptg::node *
	node() const noexcept
	{
		if (mnit_ != mnend_)
			return mnit_->second.get();

		return rnit_->second.get();
	}	

	const ptg::node &
	operator*() const
	{
		JLM_DEBUG_ASSERT(node() != nullptr);
		return *node();
	}

	const ptg::node *
	operator->() const
	{
		return node();
	}	

	constiterator &
	operator++()
	{
		if (mnit_ != mnend_)
			++mnit_;
		else
			++rnit_;

		return *this;
	}

	constiterator
	operator++(int)
	{
		constiterator tmp = *this;
		++*this;
		return tmp;
	}

	bool
	operator==(const constiterator & other) const
	{
		return mnit_ == other.mnit_
		    && rnit_ == other.rnit_;
	}

	bool
	operator!=(const constiterator & other) const
	{
		return !operator==(other);
	}

private:
	memnodemap::const_iterator mnit_;
	regnodemap::const_iterator rnit_;
	memnodemap::const_iterator mnend_;
};


/** \brief Points-to graph edge iterator
*/
class ptg::node::iterator final : public std::iterator<std::forward_iterator_tag,
	ptg::node*, ptrdiff_t> {

	friend ptg::node;

	iterator(const std::unordered_set<ptg::node*>::iterator & it)
	: it_(it)
	{}

public:
	ptg::node *
	target() const noexcept
	{
		return *it_;
	}	

	ptg::node &
	operator*() const
	{
		JLM_DEBUG_ASSERT(target() != nullptr);
		return *target();
	}

	ptg::node *
	operator->() const
	{
		return target();
	}	

	iterator &
	operator++()
	{
		++it_;
		return *this;
	}

	iterator
	operator++(int)
	{
		iterator tmp = *this;
		++*this;
		return tmp;
	}

	bool
	operator==(const iterator & other) const
	{
		return it_ == other.it_;
	}

	bool
	operator!=(const iterator & other) const
	{
		return !operator==(other);
	}

private:
	std::unordered_set<ptg::node*>::iterator it_;
};


/** \brief Points-to graph edge const iterator
*/
class ptg::node::constiterator final : public std::iterator<std::forward_iterator_tag,
	const ptg::node*, ptrdiff_t> {

	friend jlm::aa::ptg;

	constiterator(const std::unordered_set<ptg::node*>::const_iterator & it)
	: it_(it)
	{}

public:
	const ptg::node *
	target() const noexcept
	{
		return *it_;
	}	

	const ptg::node &
	operator*() const
	{
		JLM_DEBUG_ASSERT(target() != nullptr);
		return *target();
	}

	const ptg::node *
	operator->() const
	{
		return target();
	}	

	constiterator &
	operator++()
	{
		++it_;
		return *this;
	}

	constiterator
	operator++(int)
	{
		constiterator tmp = *this;
		++*this;
		return tmp;
	}

	bool
	operator==(const constiterator & other) const
	{
		return it_ == other.it_;
	}

	bool
	operator!=(const constiterator & other) const
	{
		return !operator==(other);
	}

private:
	std::unordered_set<ptg::node*>::const_iterator it_;
};

}}

#endif
