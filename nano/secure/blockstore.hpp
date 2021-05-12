#pragma once

#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/memory.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/secure/buffer.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/store/block_w_sideband.hpp>
#include <nano/secure/store/db_val.hpp>
#include <nano/secure/store/frontier_store.hpp>
#include <nano/secure/store/table_definitions.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace nano
{
class transaction;
class block_store;

/**
 * Determine the representative for this block
 */
class representative_visitor final : public nano::block_visitor
{
public:
	representative_visitor (nano::transaction const & transaction_a, nano::block_store & store_a);
	~representative_visitor () = default;
	void compute (nano::block_hash const & hash_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::transaction const & transaction;
	nano::block_store & store;
	nano::block_hash current;
	nano::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual nano::store_iterator_impl<T, U> & operator++ () = 0;
	virtual nano::store_iterator_impl<T, U> & operator-- () = 0;
	virtual bool operator== (nano::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	nano::store_iterator_impl<T, U> & operator= (nano::store_iterator_impl<T, U> const &) = delete;
	bool operator== (nano::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (nano::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator final
{
public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<nano::store_iterator_impl<T, U>> impl_a) :
		impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (nano::store_iterator<T, U> && other_a) :
		current (std::move (other_a.current)),
		impl (std::move (other_a.impl))
	{
	}
	nano::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	nano::store_iterator<T, U> & operator-- ()
	{
		--*impl;
		impl->fill (current);
		return *this;
	}
	nano::store_iterator<T, U> & operator= (nano::store_iterator<T, U> && other_a) noexcept
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	nano::store_iterator<T, U> & operator= (nano::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (nano::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (nano::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<nano::store_iterator_impl<T, U>> impl;
};

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;
};

class read_transaction_impl : public transaction_impl
{
public:
	virtual void reset () = 0;
	virtual void renew () = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	virtual void commit () = 0;
	virtual void renew () = 0;
	virtual bool contains (nano::tables table_a) const = 0;
};

class transaction
{
public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<nano::read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	void reset () const;
	void renew () const;
	void refresh () const;

private:
	std::unique_ptr<nano::read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<nano::write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	void commit ();
	void renew ();
	void refresh ();
	bool contains (nano::tables table_a) const;

private:
	std::unique_ptr<nano::write_transaction_impl> impl;
};

class ledger_cache;

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	block_store (nano::frontier_store & frontier_store_a) :
		frontier (frontier_store_a){};
	virtual ~block_store () = default;
	virtual void initialize (nano::write_transaction const &, nano::genesis const &, nano::ledger_cache &) = 0;
	virtual void block_put (nano::write_transaction const &, nano::block_hash const &, nano::block const &) = 0;
	virtual void block_raw_put (nano::write_transaction const &, std::vector<uint8_t> const &, nano::block_hash const &) = 0;
	virtual nano::block_hash block_successor (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual void block_successor_clear (nano::write_transaction const &, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> block_get (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual std::shared_ptr<nano::block> block_get_no_sideband (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual std::shared_ptr<nano::block> block_random (nano::transaction const &) = 0;
	virtual void block_del (nano::write_transaction const &, nano::block_hash const &) = 0;
	virtual bool block_exists (nano::transaction const &, nano::block_hash const &) = 0;
	virtual uint64_t block_count (nano::transaction const &) = 0;
	virtual bool root_exists (nano::transaction const &, nano::root const &) = 0;
	virtual nano::account block_account (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual nano::account block_account_calculated (nano::block const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, block_w_sideband> blocks_begin (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, block_w_sideband> blocks_begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, block_w_sideband> blocks_end () const = 0;

	const frontier_store & frontier;

	virtual void account_put (nano::write_transaction const &, nano::account const &, nano::account_info const &) = 0;
	virtual bool account_get (nano::transaction const &, nano::account const &, nano::account_info &) = 0;
	virtual void account_del (nano::write_transaction const &, nano::account const &) = 0;
	virtual bool account_exists (nano::transaction const &, nano::account const &) = 0;
	virtual size_t account_count (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> accounts_begin (nano::transaction const &, nano::account const &) const = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> accounts_begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> accounts_rbegin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> accounts_end () const = 0;

	virtual void pending_put (nano::write_transaction const &, nano::pending_key const &, nano::pending_info const &) = 0;
	virtual void pending_del (nano::write_transaction const &, nano::pending_key const &) = 0;
	virtual bool pending_get (nano::transaction const &, nano::pending_key const &, nano::pending_info &) = 0;
	virtual bool pending_exists (nano::transaction const &, nano::pending_key const &) = 0;
	virtual bool pending_any (nano::transaction const &, nano::account const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_begin (nano::transaction const &, nano::pending_key const &) const = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_end () const = 0;

	virtual nano::uint128_t block_balance (nano::transaction const &, nano::block_hash const &) = 0;
	virtual nano::uint128_t block_balance_calculated (std::shared_ptr<nano::block> const &) const = 0;
	virtual nano::epoch block_version (nano::transaction const &, nano::block_hash const &) = 0;

	virtual void unchecked_clear (nano::write_transaction const &) = 0;
	virtual void unchecked_put (nano::write_transaction const &, nano::unchecked_key const &, nano::unchecked_info const &) = 0;
	virtual void unchecked_put (nano::write_transaction const &, nano::block_hash const &, std::shared_ptr<nano::block> const &) = 0;
	virtual std::vector<nano::unchecked_info> unchecked_get (nano::transaction const &, nano::block_hash const &) = 0;
	virtual bool unchecked_exists (nano::transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a) = 0;
	virtual void unchecked_del (nano::write_transaction const &, nano::unchecked_key const &) = 0;
	virtual nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &, nano::unchecked_key const &) const = 0;
	virtual nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_end () const = 0;
	virtual size_t unchecked_count (nano::transaction const &) = 0;

	virtual void online_weight_put (nano::write_transaction const &, uint64_t, nano::amount const &) = 0;
	virtual void online_weight_del (nano::write_transaction const &, uint64_t) = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> online_weight_begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> online_weight_rbegin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> online_weight_end () const = 0;
	virtual size_t online_weight_count (nano::transaction const &) const = 0;
	virtual void online_weight_clear (nano::write_transaction const &) = 0;

	virtual void version_put (nano::write_transaction const &, int) = 0;
	virtual int version_get (nano::transaction const &) const = 0;

	virtual void pruned_put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) = 0;
	virtual void pruned_del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) = 0;
	virtual bool pruned_exists (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual nano::block_hash pruned_random (nano::transaction const & transaction_a) = 0;
	virtual size_t pruned_count (nano::transaction const & transaction_a) const = 0;
	virtual void pruned_clear (nano::write_transaction const &) = 0;
	virtual nano::store_iterator<nano::block_hash, std::nullptr_t> pruned_begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual nano::store_iterator<nano::block_hash, std::nullptr_t> pruned_begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::block_hash, std::nullptr_t> pruned_end () const = 0;

	virtual void peer_put (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual void peer_del (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual bool peer_exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const = 0;
	virtual size_t peer_count (nano::transaction const & transaction_a) const = 0;
	virtual void peer_clear (nano::write_transaction const & transaction_a) = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> peers_begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> peers_end () const = 0;

	virtual void confirmation_height_put (nano::write_transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info const & confirmation_height_info_a) = 0;
	virtual bool confirmation_height_get (nano::transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info & confirmation_height_info_a) = 0;
	virtual bool confirmation_height_exists (nano::transaction const & transaction_a, nano::account const & account_a) const = 0;
	virtual void confirmation_height_del (nano::write_transaction const & transaction_a, nano::account const & account_a) = 0;
	virtual uint64_t confirmation_height_count (nano::transaction const & transaction_a) = 0;
	virtual void confirmation_height_clear (nano::write_transaction const &, nano::account const &) = 0;
	virtual void confirmation_height_clear (nano::write_transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::confirmation_height_info> confirmation_height_begin (nano::transaction const & transaction_a, nano::account const & account_a) const = 0;
	virtual nano::store_iterator<nano::account, nano::confirmation_height_info> confirmation_height_begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::account, nano::confirmation_height_info> confirmation_height_end () const = 0;

	virtual void accounts_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::account_info>, nano::store_iterator<nano::account, nano::account_info>)> const &) const = 0;
	virtual void confirmation_height_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::confirmation_height_info>, nano::store_iterator<nano::account, nano::confirmation_height_info>)> const &) const = 0;
	virtual void pending_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::pending_key, nano::pending_info>, nano::store_iterator<nano::pending_key, nano::pending_info>)> const & action_a) const = 0;
	virtual void unchecked_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>)> const & action_a) const = 0;
	virtual void pruned_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, std::nullptr_t>, nano::store_iterator<nano::block_hash, std::nullptr_t>)> const & action_a) const = 0;
	virtual void blocks_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, block_w_sideband>, nano::store_iterator<nano::block_hash, block_w_sideband>)> const & action_a) const = 0;
	virtual void final_vote_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::qualified_root, nano::block_hash>, nano::store_iterator<nano::qualified_root, nano::block_hash>)> const & action_a) const = 0;

	virtual uint64_t block_account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;

	virtual bool final_vote_put (nano::write_transaction const & transaction_a, nano::qualified_root const & root_a, nano::block_hash const & hash_a) = 0;
	virtual std::vector<nano::block_hash> final_vote_get (nano::transaction const & transaction_a, nano::root const & root_a) = 0;
	virtual void final_vote_del (nano::write_transaction const & transaction_a, nano::root const & root_a) = 0;
	virtual size_t final_vote_count (nano::transaction const & transaction_a) const = 0;
	virtual void final_vote_clear (nano::write_transaction const &, nano::root const &) = 0;
	virtual void final_vote_clear (nano::write_transaction const &) = 0;
	virtual nano::store_iterator<nano::qualified_root, nano::block_hash> final_vote_begin (nano::transaction const & transaction_a, nano::qualified_root const & root_a) const = 0;
	virtual nano::store_iterator<nano::qualified_root, nano::block_hash> final_vote_begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::qualified_root, nano::block_hash> final_vote_end () const = 0;

	virtual unsigned max_block_write_batch_num () const = 0;

	virtual bool copy_db (boost::filesystem::path const & destination) = 0;
	virtual void rebuild_db (nano::write_transaction const & transaction_a) = 0;

	/** Not applicable to all sub-classes */
	virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds){};
	virtual void serialize_memory_stats (boost::property_tree::ptree &) = 0;

	virtual bool init_error () const = 0;

	/** Start read-write transaction */
	virtual nano::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_to_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) = 0;

	/** Start read-only transaction */
	virtual nano::read_transaction tx_begin_read () const = 0;

	virtual std::string vendor_get () const = 0;
};

std::unique_ptr<nano::block_store> make_store (nano::logger_mt & logger, boost::filesystem::path const & path, bool open_read_only = false, bool add_db_postfix = false, nano::rocksdb_config const & rocksdb_config = nano::rocksdb_config{}, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{}, bool backup_before_upgrade = false);
}

namespace std
{
template <>
struct hash<::nano::tables>
{
	size_t operator() (::nano::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
};
}
