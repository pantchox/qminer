/**
 * GLib - General C++ Library
 * 
 * Copyright (C) 2014 Jozef Stefan Institute
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef GIX_H
#define GIX_H

// this file depends only on base.h

/////////////////////////////////////////////////
// Forward-declarations
template <class TKey, class TItem> class TGix;
template <class TKey, class TItem> class TGixStorageLayer;

/////////////////////////////////////////////////
// General-Inverted-Index-Merger
template <class TKey, class TItem>
class TGixMerger {
protected:
    TCRef CRef;
    typedef TPt<TGixMerger<TKey, TItem> > PGixMerger;

public:
	virtual ~TGixMerger() { }

    virtual void Union(TVec<TItem>& MainV, const TVec<TItem>& JoinV) const = 0;
    virtual void Intrs(TVec<TItem>& MainV, const TVec<TItem>& JoinV) const = 0;
    virtual void Minus(const TVec<TItem>& MainV, 
		const TVec<TItem>& JoinV, TVec<TItem>& ResV) const = 0;
	virtual void Merge(TVec<TItem>& ItemV) const = 0;
    virtual void Def(const TKey& Key, TVec<TItem>& MainV) const = 0;
	virtual bool IsLt(const TItem& Item1, const TItem& Item2) const = 0;

    friend class TPt<TGixMerger<TKey, TItem> >;
};

/////////////////////////////////////////////////
// General-Inverted-Index-Default-Merger
template <class TKey, class TItem>
class TGixDefMerger : public TGixMerger<TKey, TItem> {
private:
    typedef TPt<TGixMerger<TKey, TItem> > PGixMerger;

public:
	static PGixMerger New() { return new TGixDefMerger<TKey, TItem>(); }

    void Union(TVec<TItem>& MainV, const TVec<TItem>& JoinV) const { MainV.Union(JoinV); }
    void Intrs(TVec<TItem>& MainV, const TVec<TItem>& JoinV) const { MainV.Intrs(JoinV); }
    void Minus(const TVec<TItem>& MainV, const TVec<TItem>& JoinV, 
        TVec<TItem>& ResV) const { MainV.Diff(JoinV, ResV); }
	void Merge(TVec<TItem>& ItemV) const { ItemV.Merge(); }
    void Def(const TKey& Key, TVec<TItem>& MainV) const  { }
	bool IsLt(const TItem& Item1, const TItem& Item2) const { return Item1 < Item2; }
};

/////////////////////////////////////////////////
// General-Inverted-Index Key-To-String
template <class TKey>
class TGixKeyStr {
protected:
	TCRef CRef;
	typedef TPt<TGixKeyStr<TKey> > PGixKeyStr;

public:
	virtual ~TGixKeyStr() { }
	static PGixKeyStr New() { return new TGixKeyStr<TKey>; }

	// by default we cannot assume much about key, so just return empty string
	virtual TStr GetKeyNm(const TKey& Key) const { return TStr(); }

    friend class TPt<TGixKeyStr<TKey> >;
};

/////////////////////////////////////////////////
// General-Inverted-Index Item-Set
//
// Supports splitting of data into child vectors.
// Assumes child vectors are individually and globaly merged

template <class TKey, class TItem>
class TGixItemSet {
private:
    TCRef CRef;
    typedef TPt<TGixItemSet<TKey, TItem> > PGixItemSet;
    typedef TPt<TGixMerger<TKey, TItem> > PGixMerger;
private:

	/// This struct contans statistics about child vector
	class TGixItemSetChildInfo {
	public:
		TItem MinVal;
		TItem MaxVal;
		TInt Len;
		TBlobPt Pt;
		TBool MergedP;

		/// default constructor
		TGixItemSetChildInfo()
			: Len(0), MergedP(true) {}

		/// constructor with values
		TGixItemSetChildInfo(const TItem& _MinVal, const TItem& _MaxVal,
			const TInt& _Len, const TBlobPt& _Pt, const TBool& _MergedP)
			: MinVal(_MinVal), MaxVal(_MaxVal), Len(_Len), Pt(_Pt), MergedP(_MergedP) { }

		/// constructor for serialization
		TGixItemSetChildInfo(TSIn& SIn) :
			MinVal(SIn), MaxVal(SIn), Len(SIn), Pt(SIn), MergedP(SIn) {}

		/// deserialize from stream
		void Load(TSIn& SIn) {
			MinVal.Load(SIn);
			MaxVal.Load(SIn);
			Len.Load(SIn);
			Pt.Load(SIn);
			MergedP.Load(SIn);
		}
		/// serialize to stream
		void Save(TSOut& SOut) const {
			MinVal.Save(SOut);
			MaxVal.Save(SOut);
			Len.Save(SOut);
			Pt.Save(SOut);
			MergedP.Save(SOut);
		}
	};

	/// The key of this itemset
    TKey ItemSetKey;
	/// "Working buffer" of items of this itemset - could be only part of them, other can be stored in child vectors
    TVec<TItem> ItemV;
	/// Combined count - from this itemset and children
	int TotalCnt;

	// optional data about child vectors - will be populated only for frequent keys
	TVec<TGixItemSetChildInfo> Children;

	// optional list of child vector contents - will be populated only for frequent keys
	mutable TVec<TVec<TItem>> ChildrenData;

    // for keeping the ItemV unique and sorted
    TBool MergedP;
	// pointer to merger that will merge this itemset
    PGixMerger Merger;
	// pointer to gix - the storage-layer (serialization of self, loading children, notifying about chnages...)
	const TGix<TKey, TItem> *Gix;


	/// Load single child vector into memory if not present already
	void LoadChildVector(int i) const {
		if (Children[i].Len != ChildrenData[i].Len()) {
			Gix->GetChildVector(Children[i].Pt, ChildrenData[i]);
		}
	}
	/// Load all child vectors into memory and get pointers to them
	void LoadChildVectors() {
		for (int i = 0; i < Children.Len(); i++) {
			LoadChildVector(i);
		}
	}

	/// Refresh total count
	void RecalcTotalCnt() {
		TotalCnt = ItemV.Len();
		for (int i = 0; i < Children.Len(); i++)
			TotalCnt += Children[i].Len;
	}

public:
	/// Standard constructor
	TGixItemSet(const TKey& _ItemSetKey, const PGixMerger& _Merger, const TGix<TKey, TItem>* _Gix) :
		ItemSetKey(_ItemSetKey), MergedP(true), Merger(_Merger), Gix(_Gix), TotalCnt(0) {}
	/// Standard factory method
	static PGixItemSet New(const TKey& ItemSetKey, const PGixMerger& Merger, const TGix<TKey, TItem>* Gix) {
		return new TGixItemSet(ItemSetKey, Merger, Gix);
	}

	/// Constructor for deserialization
	TGixItemSet(TSIn& SIn, const PGixMerger& _Merger, const TGix<TKey, TItem>* _Gix) :
		ItemSetKey(SIn), ItemV(SIn), Children(SIn), MergedP(true), Merger(_Merger), Gix(_Gix)  { 
		for (int i = 0; i < Children.Len(); i++) {
			ChildrenData.Add(TVec<TItem>());
		};
		RecalcTotalCnt();
	}
	/// Standard factory method for deserialization
	static PGixItemSet Load(TSIn& SIn, const PGixMerger& Merger, const TGix<TKey, TItem>* Gix) {
		return new TGixItemSet(SIn, Merger, Gix);
	}
	/// Saves this itemset to output stream
    void Save(TSOut& SOut);

    // functions used by TCache
	// TODO child-vector size is not included!!!
    int GetMemUsed() const {
        return ItemSetKey.GetMemUsed() + ItemV.GetMemUsed() 
			+ Children.GetMemUsed() + ChildrenData.GetMemUsed()
			+ sizeof(TBool) + sizeof(int) + sizeof(PGixMerger);
	}
    void OnDelFromCache(const TBlobPt& BlobPt, void* Gix);

    // key & items
    const TKey& GetKey() const { return ItemSetKey; }
	void AddItem(const TItem& NewItem);
	void AddItemV(const TVec<TItem>& NewItemV);
	void OverrideItems(const TVec<TItem>& NewItemV, int From, int Len);
	/// Get number of items (including child itemsets)
	int GetItems() const { return TotalCnt;	}
	/// Get item at given index (including child itemsets)
	const TItem& GetItem(const int& ItemN) const;

    //const TVec<TItem>& GetItemV() const { return ItemV; }
	void AppendItemSet(const TPt<TGixItemSet>& Src);
	/// Get items into vector
	void GetItemV(TVec<TItem>& _ItemV);
	/// Delete specified item from this itemset
	void DelItem(const TItem& Item);
	/// Clear all items from this itemset
    void Clr();
	/// Pack/merge this itemset
    void Def();
	/// Pack/merge this itemset - just workng buffer
	void DefLocal();

	/// Tests if current itemset is full and subsequent item should be pushed to children
	bool IsFull() { 
#ifdef GIX_TEST
		return (ItemV.Len() >= len_to_split);
#else
		return (ItemV.GetMemUsed() > 10 * 1024 * 1024); // 10 MB
#endif
	}

	friend class TPt<TGixItemSet>; 
#ifdef GIX_TEST
	friend class XTest;

	void Print() {
		printf("TotalCnt=%d\n", TotalCnt);
		printf("len=%d\n", ItemV.Len());
		for (int i = 0; i < ItemV.Len(); i++) {
			printf("   %d=%d\n", i, ItemV[i]);
		}
		for (int j = 0; j < Children.Len(); j++) {
			printf("   *** child %d\n", j);
			printf("   *** len %d\n", Children[j].Len);
			for (int i = 0; i < ChildrenData[j].Len(); i++) {
				printf("      %d=%d\n", i, ChildrenData[j][i]);
			}
		}
	}

	static int len_to_split;
#endif
};

template <class TKey, class TItem>
const TItem& TGixItemSet<TKey, TItem>::GetItem(const int& ItemN) const {
	AssertR(ItemN >= 0 && ItemN < TotalCnt, TStr() + "Index: " + TInt::GetStr(ItemN) + ", TotalCnt: " + TInt::GetStr(TotalCnt));
	if (ItemN < ItemV.Len()) {
		// data is in this itemset
		return ItemV[ItemN];
	}
	// data is in child itemsets
	int index = ItemN - ItemV.Len();
	for (int i = 0; i < ChildrenLen.Len(); i++) {
		if (index < ChildrenLen[i]) {
			// load child vector only if needed
			LoadChildVector(i);
			return ChildrenData[i][index];
		}
		index -= ChildrenLen[i];
	}
	return ItemV[-1]; // will trigger error, should not happen anyway
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::Save(TSOut& SOut) { 

	// make sure all is merged before saving
	Def();

	// save child vectors separately
	for (int i = 0; i < Children.Len(); i++) {
		if (ChildrenData[i].Len() > 0) {
			// TODO check dirty bit
			Children[i].Pt = Gix->StoreChildVector(Children[i].Pt, ChildrenData[i]);
		}
	}
	// save item key and set
	ItemSetKey.Save(SOut);
	ItemV.Save(SOut);
	Children.Save(SOut);
}


template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::OnDelFromCache(const TBlobPt& BlobPt, void* Gix) {
	if (!((TGix<TKey, TItem>*)Gix)->IsReadOnly()) {
		((TGix<TKey, TItem>*)Gix)->StoreItemSet(BlobPt);
    } 
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::AddItem(const TItem& NewItem) { 
    const int OldSize = GetMemUsed();
	if (IsFull()) {
		// work-buffer is full, send data into child vector
		bool store = true;
		if (!MergedP) {
			// first perform local-merge
			int old_len = ItemV.Len();
			Merger->Merge(ItemV);
			int new_len = ItemV.Len();
			
			// check if global MergedP is now achieved
			if (Children.Len() > 0) {
				if (Merger->IsLt(Children.Last().MaxVal, ItemV[0])) {
					MergedP = true; // work-buffer contents was "greater than" data in child vectors
				} else {
					Def(); // perform global merge
					new_len = ItemV.Len();
				}
			} else {
				MergedP = true; // no children, local merge was also global merge
			}
			store = ((double)new_len / (double)old_len > 0.9); // check if itemset is still large
		}
		
		if (store) {
			// push work-buffer into children array
			TGixItemSetChildInfo child_info(ItemV[0], ItemV.Last(), ItemV.Len(), Gix->EnlistChildVector(ItemV), MergedP);
			Children.Add(child_info);
			ChildrenData.Add(TVec<TItem>()); // TODO here we add empty child, should we add the existing contents? it has just been saved to disk...
			ItemV.Clr();
		}
		RecalcTotalCnt(); // work buffer might have been merged
	}

	if (MergedP) {
		// if itemset is merged and the newly added item is bigger than the last one
		// the itemset remains merged
		if (ItemV.Len() == 0 && Children.Len() == 0) {
			MergedP = true;
		} else if (ItemV.Len() == 0 && Children.Len() != 0) {
			MergedP = Merger->IsLt(Children.Last().MaxVal, NewItem);
		} else {
			MergedP = Merger->IsLt(ItemV.Last(), NewItem);
		}
	}
	ItemV.Add(NewItem);
	TotalCnt++;

	// notify cache that this item grew
	Gix->AddToNewCacheSizeInc(GetMemUsed() - OldSize);
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::AddItemV(const TVec<TItem>& NewItemV) { 
	for (int i = 0; i < NewItemV.Len(); i++) {
		AddItem(NewItemV[i]);
	}
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::AppendItemSet(const TPt<TGixItemSet>& Src) {
	Src->LoadChildVectors();
	// first, now process children if present
	if (Src->Children.Len() > 0) {
		// merge each of them
		for (int i = 0; i < ChildrenData.Len(); i++) {
			AddItemV(Src->ChildrenData[i]);
		}
	}
	// now get work buffer
	AddItemV(Src->ItemV);
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::GetItemV(TVec<TItem>& _ItemV) {
	// TODO think about adding memcpy-enabled methods to TVec

	if (Children.Len() > 0) {
		// collect data from child itemsets
		LoadChildVectors();
		for (int i = 0; i < Children.Len(); i++) {
			_ItemV.AddV(ChildrenData[i]);
		}
	}
	_ItemV.AddV(ItemV);
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::DelItem(const TItem& Item) {
	// TODO rewrite - delete should add special "delete command" to data, not perform true merge

	Def();
	const int OldSize = GetMemUsed();
	ItemV.DelIfIn(Item);
	if (Children.Len()>0) {
		LoadChildVectors();
		for (int i = 0; i < Children.Len(); i++) {
			ChildrenData[i].DelIfIn(Item);
			ChildrenLen[i] = ChildrenData[i].Len();
		}
	}
	RecalcTotalCnt();
	Gix->AddToNewCacheSizeInc(GetMemUsed() - OldSize);
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::Clr() { 
	const int OldSize = GetMemUsed();
	if (Children.Len() > 0) {
		for (int i = 0; i < Children.Len(); i++) {
			Gix->DeleteChildVector(Children[i].Pt);
		}
		ChildrenData.Clr();
		Children.Clr();
	}
	ItemV.Clr();
	MergedP = true;
	TotalCnt = 0;
    Gix->AddToNewCacheSizeInc(GetMemUsed() - OldSize);
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::Def() { 
	// call merger to pack items, if not merged yet 
	if (!MergedP) {  

		// TODO first execute deletes

		Merger->Merge(ItemV); // first local merge
		if (Children.Len() > 0 && ItemV.Len() > 0) {		
			// detect the first child that needs to be merged
			TItem ItemVMin = ItemV[0];
			int first_child_to_merge = Children.Len() - 1;
			while (first_child_to_merge >= 0 && Merger->IsLt(ItemVMin, Children[first_child_to_merge].MaxVal)) {
				first_child_to_merge--;
			}
			first_child_to_merge++;
			// collect all data from subsequent child vectors and work-buffer
			TVec<TItem> MergedItems;  
			for (int i = first_child_to_merge; i < Children.Len(); i++) {
				LoadChildVector(i);
				MergedItems.AddV(ChildrenData[i]);
			}
			MergedItems.AddV(ItemV);
			Merger->Merge(MergedItems);
			
			// now save them back
			int curr_index = 0;
			int remaining = MergedItems.Len() - curr_index;
			int child_index = first_child_to_merge;
			while (curr_index < MergedItems.Len()) {
				if (child_index < Children.Len() && remaining > Children[child_index].Len) {
					ChildrenData[child_index].Clr();
					MergedItems.GetSubValV(curr_index, curr_index + Children[child_index].Len - 1, ChildrenData[child_index]);
					curr_index += Children[child_index].Len;
					remaining = MergedItems.Len() - curr_index;
					child_index++;
				} else {
					// the remaining data fits into work-buffer
					ItemV.Clr();
					MergedItems.GetSubValV(curr_index, curr_index + remaining - 1, ItemV);
					break;
				}
			}

			// clear children that became empty - kill'em all
			int first_empty_child = child_index;
			while (child_index < Children.Len()) { // remove them from BLOB storage
				Gix->DeleteChildVector(Children[child_index++].Pt); // remove from storage
			}
			if (first_empty_child < Children.Len()) { // now remove their stuff from memory
				Children.Del(first_empty_child, Children.Len() - 1);
				ChildrenData.Del(first_empty_child, ChildrenData.Len() - 1);
			}
		} else if (Children.Len() > 0 && ItemV.Len() == 0) {
			// nothing, children should already be merged and work-buffer is empty
		} else {
			// nothing, there are no children and work-buffer has already been merged
		}
		RecalcTotalCnt();
		MergedP = true;
	}
}

template <class TKey, class TItem>
void TGixItemSet<TKey, TItem>::DefLocal() {
	// call merger to pack items in work buffer, if not merged yet 
	if (!MergedP) {
		Merger->Merge(ItemV); // perform local merge
		if (Children.Len() > 0 && ItemV.Len() > 0) {
			if (Merger->IsLt(Children.Last().MaxVal, ItemV[0])) {
				// local merge achieved global merge
				MergedP = true;
			}
		} else {
			MergedP = true;
		}
		RecalcTotalCnt();
	}
}

/////////////////////////////////////////////////
// General-Inverted-Index
template <class TKey, class TItem>
class TGix {
private:
    TCRef CRef;
    typedef TPt<TGix<TKey, TItem> > PGix;
	//typedef TPt<TGixStorageLayer<TKey, TItem> > PGixStorageLayer;
    typedef TPt<TGixItemSet<TKey, TItem> > PGixItemSet;
    typedef TPt<TGixMerger<TKey, TItem> > PGixMerger;
    typedef TGixDefMerger<TKey, TItem> _TGixDefMerger;
	typedef TPt<TGixKeyStr<TKey> > PGixKeyStr;
private:
	/// File access mode - checked during index operations
    TFAccess Access;
	/// Name of the main file
    TStr GixFNm;
	/// Name of the BLOB file
    TStr GixBlobFNm;
	/// mapping between key and BLOB pointer
    THash<TKey, TBlobPt> KeyIdH; 

	/// record merger, used for packing data vectors
	PGixMerger Merger;

	/// cache for BLOB data, mapping between pointers and data
	mutable TCache<TBlobPt, PGixItemSet> ItemSetCache;
	/// BLOB handler
	PBlobBs ItemSetBlobBs;

	int64 CacheResetThreshold;
	mutable int64 NewCacheSizeInc;
	/// flag if cache is full
	bool CacheFullP;

	// returns pointer to this object (used in cache call-backs)
	void* GetVoidThis() const { return (void*)this; }

    /// asserts if we are allowed to change this index
    void AssertReadOnly() const {
        EAssertR(((Access==faCreate)||(Access==faUpdate)), 
            "Index opened in Read-Only mode!"); }

    /// get keyid of a given key and create it if does not exist
    TBlobPt AddKeyId(const TKey& Key);
	/// get keyid of a given key
	TBlobPt GetKeyId(const TKey& Key) const;

	/// get child vector for given blob pointer
	void GetChildVector(const TBlobPt& Pt, TVec<TItem>& Dest) const;
	/// for storing child vectors to blob
	TBlobPt StoreChildVector(const TBlobPt& ExistingKeyId, const TVec<TItem>& Data) const;
	/// for deleting child vectors from cache and blob
	void DeleteChildVector(const TBlobPt& KeyId) const;
	/// For enlisting new child vectors into blob
	TBlobPt EnlistChildVector(const TVec<TItem>& Data) const;

public:
    TGix(const TStr& Nm, const TStr& FPath = TStr(), 
		const TFAccess& _Access = faRdOnly, const int64& CacheSize = 100000000, 
		const PGixMerger& _Merger = _TGixDefMerger::New() );
    static PGix New(const TStr& Nm, const TStr& FPath = TStr(), 
		const TFAccess& Access = faRdOnly, const int64& CacheSize = 100000000, 
		const PGixMerger& Merger = _TGixDefMerger::New()) {
            return new TGix(Nm, FPath, Access, CacheSize, Merger); }
	
	~TGix();

    // Gix properties
    bool IsReadOnly() const { return Access == faRdOnly; }
	bool IsCacheFullP() const { return CacheFullP; }
	TStr GetFPath() const { return GixFNm.GetFPath(); }
	int64 GetMxCacheSize() const { return GetMxMemUsed(); }

    /// do we have Key in the index?
    bool IsKey(const TKey& Key) const { return KeyIdH.IsKey(Key); }
    /// number of keys in the index
    int GetKeys() const { return KeyIdH.Len(); }
    /// sort keys
    void SortKeys() { KeyIdH.SortByKey(true); }

    /// get item set for given key
	PGixItemSet GetItemSet(const TKey& Key) const;
	/// get item set for given BLOB pointer
	PGixItemSet GetItemSet(const TBlobPt& Pt) const;
	/// for storing item sets from cache to blob
	void StoreItemSet(const TBlobPt& KeyId);
	/// for deleting item sets from cache and blob
	void DeleteItemSet(const TBlobPt& KeyId) const;
	/// For enlisting new itemsets into blob
	TBlobPt EnlistItemSet(const PGixItemSet& ItemSet) const;

    /// adding new item to the inverted index
    void AddItem(const TKey& Key, const TItem& Item);
    /// adding new items to the inverted index
    void AddItemV(const TKey& Key, const TVec<TItem>& ItemV);
    // delete one item
    void DelItem(const TKey& Key, const TItem& Item);
	/// clears items
    void Clr(const TKey& Key);
	/// flush all data from cache to disk
	void Flush() { ItemSetCache.FlushAndClr(); }

	
    // traversing keys
    /// get first key id
	int FFirstKeyId() const { return KeyIdH.FFirstKeyId(); }
	/// get next key id
    bool FNextKeyId(int& KeyId) const { return KeyIdH.FNextKeyId(KeyId); }
	/// get key for given key id
    const TKey& GetKey(const int& KeyId) const { return KeyIdH.GetKey(KeyId); }

    // merges another index with same key and item vocabulary
    void MergeIndex(const PGix& TmpGix);

    /// get amount of memory currently used
    int64 GetMemUsed() const { 
        return int64(sizeof(TFAccess) + GixFNm.GetMemUsed() + GixBlobFNm.GetMemUsed()) + 
            int64(KeyIdH.GetMemUsed()) + int64(ItemSetCache.GetMemUsed()); }
	int GetNewCacheSizeInc() const { return NewCacheSizeInc; }
	int GetCacheSize() const { return ItemSetCache.GetMemUsed(); }
	int64 GetMxMemUsed() const { return ItemSetCache.GetMxMemUsed(); }
	bool IsCacheFull() const { return CacheFullP; }
	void RefreshMemUsed();
	void AddToNewCacheSizeInc(int64 diff) const { NewCacheSizeInc += diff; }


	/// print statistics for index keys
	void SaveTxt(const TStr& FNm, const PGixKeyStr& KeyStr) const;

    friend class TPt<TGix>;
    friend class TGixItemSet<TKey, TItem>;
#ifdef GIX_TEST
	friend class XTest;
#endif
};

template <class TKey, class TItem>
TBlobPt TGix<TKey, TItem>::AddKeyId(const TKey& Key) { 
    if (IsKey(Key)) { return KeyIdH.GetDat(Key); }
    // we don't have this key, create an empty item set and return pointer to it
    AssertReadOnly(); // check if we are allowed to write
	PGixItemSet ItemSet = TGixItemSet<TKey, TItem>::New(Key, Merger, this);
	TBlobPt KeyId = EnlistItemSet(ItemSet);
    KeyIdH.AddDat(Key, KeyId); // remember the new key and its Id
    return KeyId;
}

template <class TKey, class TItem>
TBlobPt TGix<TKey, TItem>::GetKeyId(const TKey& Key) const { 
    if (IsKey(Key)) { return KeyIdH.GetDat(Key); }
    // we don't have this key, return empty pointer
    return TBlobPt();
}

template <class TKey, class TItem>
TGix<TKey, TItem>::TGix(const TStr& Nm, const TStr& FPath, const TFAccess& _Access, 
  const int64& CacheSize, const TPt<TGixMerger<TKey, TItem> >& _Merger): Access(_Access),
  ItemSetCache(CacheSize, 1000000, GetVoidThis()), 
  Merger(_Merger) {

    // filenames of the GIX datastore
    GixFNm = TStr::GetNrFPath(FPath) + Nm.GetFBase() + ".Gix";
    GixBlobFNm = TStr::GetNrFPath(FPath) + Nm.GetFBase() + ".GixDat";
	
    if (Access == faCreate) {
        // creating a new Gix
        ItemSetBlobBs = TMBlobBs::New(GixBlobFNm, faCreate);
    } else {
        // loading an old Gix and getting it ready for search and update
        EAssert((Access == faUpdate) || (Access == faRdOnly) || (Access == faRestore));
        // load Gix from GixFNm
        TFIn FIn(GixFNm); 
		KeyIdH.Load(FIn);
        // load ItemSets from GixBlobFNm
        ItemSetBlobBs = TMBlobBs::New(GixBlobFNm, Access);
    }
	//GixSL = TGixStorageLayer<TKey, TItem>::New(GixBlobFNm, Access, 1000000, Merger);

    CacheResetThreshold = int64(0.1 * double(CacheSize));
    NewCacheSizeInc = 0; 
	CacheFullP = false;
}

template <class TKey, class TItem>
TGix<TKey, TItem>::~TGix() {
    if ((Access == faCreate) || (Access == faUpdate)) {
        // flush all the latest changes in cache to the disk
		ItemSetCache.Flush();
		// save the rest to GixFNm
        TFOut FOut(GixFNm); 
		KeyIdH.Save(FOut);
    }
}

template <class TKey, class TItem>
TPt<TGixItemSet<TKey, TItem> > TGix<TKey, TItem>::GetItemSet(const TKey& Key) const {
    TBlobPt KeyId = GetKeyId(Key);
	return GetItemSet(KeyId);
}
template <class TKey, class TItem>
TPt<TGixItemSet<TKey, TItem> > TGix<TKey, TItem>::GetItemSet(const TBlobPt& KeyId) const {
	if (KeyId.Empty()) { return NULL; }
	PGixItemSet ItemSet;
	if (!ItemSetCache.Get(KeyId, ItemSet)) {
		// have to load it from the hard drive...
		PSIn ItemSetSIn = ItemSetBlobBs->GetBlob(KeyId);
		ItemSet = TGixItemSet<TKey, TItem>::Load(*ItemSetSIn, Merger, this);
	}
	// bring the itemset to the top of the cache
	ItemSetCache.Put(KeyId, ItemSet);
	return ItemSet;
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::StoreItemSet(const TBlobPt& KeyId) {
	AssertReadOnly(); // check if we are allowed to write
	// get the pointer to the item set
	PGixItemSet ItemSet;
	EAssert(ItemSetCache.Get(KeyId, ItemSet));
	// store the current version to the blob
	TMOut MOut;
	ItemSet->Save(MOut);
	TBlobPt NewKeyId = ItemSetBlobBs->PutBlob(KeyId, MOut.GetSIn());
	// and update the KeyId in the hash table
	KeyIdH.GetDat(ItemSet->GetKey()) = NewKeyId;
}

template <class TKey, class TItem>
TBlobPt TGix<TKey, TItem>::EnlistItemSet(const PGixItemSet& ItemSet) const {
	AssertReadOnly(); // check if we are allowed to write
	TMOut MOut;
	ItemSet->Save(MOut);
	TBlobPt res = ItemSetBlobBs->PutBlob(MOut.GetSIn());
	//printf("enlisted new itemset to storage: %d %d \n", res.Addr, res.Seg);
	return res;
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::AddItem(const TKey& Key, const TItem& Item) {
    AssertReadOnly(); // check if we are allowed to write
    // get the key handle
    TBlobPt KeyId = AddKeyId(Key);
    // load the current item set
	PGixItemSet ItemSet = GetItemSet(Key);
    ItemSet->AddItem(Item);
    // check if we have to drop anything from the cache
    RefreshMemUsed();
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::AddItemV(const TKey& Key, const TVec<TItem>& ItemV) {
    AssertReadOnly(); // check if we are allowed to write
    // get the key handle
    TBlobPt KeyId = AddKeyId(Key);
    // load the current item set
    PGixItemSet ItemSet = GetItemSet(Key);	
	// add the new items to the set and update the size of new items
    ItemSet->AddItemV(ItemV);    
	// check if we have to drop anything from the cache
	RefreshMemUsed();
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::DelItem(const TKey& Key, const TItem& Item) {
    AssertReadOnly(); // check if we are allowed to write
    if (IsKey(Key)) { // check if this key exists
        // load the current item set
		PGixItemSet ItemSet = GetItemSet(Key);
        // clear the items from the ItemSet
        ItemSet->DelItem(Item);
    }
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::Clr(const TKey& Key) {
    AssertReadOnly(); // check if we are allowed to write
    if (IsKey(Key)) { // check if this key exists
        // load the current item set
        PGixItemSet ItemSet = GetItemSet(Key);
        // clear the items from the ItemSet
        ItemSet->Clr();
    }
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::MergeIndex(const TPt<TGix<TKey, TItem> >& TmpGix) {
    // merge itemsets
    const int TmpKeys = TmpGix->GetKeys();
    int TmpKeyId = TmpGix->FFirstKeyId();
    while (TmpGix->FNextKeyId(TmpKeyId)) {
        const TKey& TmpKey = TmpGix->GetKey(TmpKeyId);
        PGixItemSet TmpItemSet = TmpGix->GetItemSet(TmpKey);
		PGixItemSet MyItemSet = GetItemSet(TmpKey);
		MyItemSet->AppendItemSet(TmpItemSet);
        //AddItemV(ItemSet->GetKey(), ItemSet->GetItemV());
        if (TmpKeyId % 1000 == 0) { 
            printf("[%d/%d]\r", TmpKeyId, TmpKeys); }
    }
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::SaveTxt(const TStr& FNm, const PGixKeyStr& KeyStr) const {
	TFOut FOut(FNm);
	// iterate over all the keys
	printf("Starting Gix SaveTxt\n");
	int KeyId = FFirstKeyId();
	int KeyN = 0; const int Keys = GetKeys();
	while (FNextKeyId(KeyId)) {
		if (KeyN % 1000 == 0) { printf("%d / %d\r", KeyN, Keys); } KeyN++;
		// get key and associated item set
		const TKey& Key = GetKey(KeyId);
		PGixItemSet ItemSet = GetItemSet(Key);
		// get statistics
		TStr KeyNm = KeyStr->GetKeyNm(Key);
		const int Items = ItemSet->GetItems();
		const int MemUsed = ItemSet->GetMemUsed();
		// output statistics
		FOut.PutStrFmtLn("%s\t%d\t%d", KeyNm .CStr(), Items, MemUsed);
	}
	printf("Done: %d / %d\n", Keys, Keys);
}

/// for storing vectors to blob
template <class TKey, class TItem>
TBlobPt TGix<TKey, TItem>::StoreChildVector(const TBlobPt& ExistingKeyId, const TVec<TItem>& Data) const {
	AssertReadOnly(); // check if we are allowed to write
	// store the current version to the blob
	TMOut MOut;
	Data.Save(MOut);
	return ItemSetBlobBs->PutBlob(ExistingKeyId, MOut.GetSIn());
}

/// for deleting child vector from blob
template <class TKey, class TItem>
void TGix<TKey, TItem>::DeleteChildVector(const TBlobPt& KeyId) const {
	AssertReadOnly(); // check if we are allowed to write
	ItemSetBlobBs->DelBlob(KeyId);  // free space in BLOB
}

/// For enlisting new child vectors into blob
template <class TKey, class TItem>
TBlobPt TGix<TKey, TItem>::EnlistChildVector(const TVec<TItem>& Data) const {
	AssertReadOnly(); // check if we are allowed to write
	TMOut MOut;
	Data.Save(MOut);
	TBlobPt res = ItemSetBlobBs->PutBlob(MOut.GetSIn());
	return res;
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::GetChildVector(const TBlobPt& KeyId, TVec<TItem>& Dest) const {
	if (KeyId.Empty()) { return; }
	PSIn ItemSetSIn = ItemSetBlobBs->GetBlob(KeyId);
	Dest.Load(*ItemSetSIn);
}

template <class TKey, class TItem>
void TGix<TKey, TItem>::RefreshMemUsed() {
	// check if we have to drop anything from the cache
	if (NewCacheSizeInc > CacheResetThreshold) {
		printf("Cache clean-up [%s] ... ", TUInt64::GetMegaStr(NewCacheSizeInc).CStr());
		// pack all the item sets
		TBlobPt BlobPt;
		PGixItemSet ItemSet;
		void* KeyDatP = ItemSetCache.FFirstKeyDat();
		while (ItemSetCache.FNextKeyDat(KeyDatP, BlobPt, ItemSet)) { 
			ItemSet->DefLocal(); 
		}
		// clean-up cache
		CacheFullP = ItemSetCache.RefreshMemUsed();
		NewCacheSizeInc = 0;
		const uint64 NewSize = ItemSetCache.GetMemUsed();
		printf("Done [%s]\n", TUInt64::GetMegaStr(NewSize).CStr());
	}
}

/////////////////////////////////////////////////
// General-Inverted-Index Expression-Item
typedef enum { getUndef, getEmpty, getOr, getAnd, getNot, getKey } TGixExpType;

template <class TKey, class TItem>
class TGixExpItem {
private:
    TCRef CRef;
    typedef TPt<TGixExpItem<TKey, TItem> > PGixExpItem;
    typedef TPt<TGixItemSet<TKey, TItem> > PGixItemSet;
    typedef TPt<TGix<TKey, TItem> > PGix;
    typedef TGixDefMerger<TKey, TItem> _TGixDefMerger;
private:
    TGixExpType ExpType;
    PGixExpItem LeftExpItem;
    PGixExpItem RightExpItem;
    TKey Key;

    TGixExpItem(const TGixExpType& _ExpType, const PGixExpItem& _LeftExpItem, 
      const PGixExpItem& _RightExpItem): ExpType(_ExpType), 
        LeftExpItem(_LeftExpItem), RightExpItem(_RightExpItem) { }
    TGixExpItem(const TKey& _Key): ExpType(getKey), Key(_Key) { }
    TGixExpItem(): ExpType(getEmpty) { }
	TGixExpItem(const TGixExpItem& ExpItem): ExpType(ExpItem.ExpType),
		LeftExpItem(ExpItem.LeftExpItem), RightExpItem(ExpItem.RightExpItem),
		Key(ExpItem.Key) { }

	void PutAnd(const PGixExpItem& _LeftExpItem, const PGixExpItem& _RightExpItem);
	void PutOr(const PGixExpItem& _LeftExpItem, const PGixExpItem& _RightExpItem);
public:
	// elementary operations
    static PGixExpItem NewOr(const PGixExpItem& LeftExpItem, const PGixExpItem& RightExpItem) { 
        return new TGixExpItem(getOr, LeftExpItem, RightExpItem); }
    static PGixExpItem NewAnd(const PGixExpItem& LeftExpItem, const PGixExpItem& RightExpItem) { 
        return new TGixExpItem(getAnd, LeftExpItem, RightExpItem); }
    static PGixExpItem NewNot(const PGixExpItem& RightExpItem) { 
        return new TGixExpItem(getNot, NULL, RightExpItem); }
    static PGixExpItem NewItem(const TKey& Key) { 
        return new TGixExpItem(Key); }
    static PGixExpItem NewEmpty() { 
        return new TGixExpItem(); }

	// some predifined structures
	static PGixExpItem NewAndV(const TVec<PGixExpItem>& ExpItemV);
	static PGixExpItem NewOrV(const TVec<PGixExpItem>& ExpItemV);
	static PGixExpItem NewAndV(const TVec<TKey>& KeyV);
	static PGixExpItem NewOrV(const TVec<TKey>& KeyV);

    bool IsEmpty() const { return (ExpType == getEmpty); }
    TGixExpType GetExpType() const { return ExpType; }
	TKey GetKey() const { return Key; }
	PGixExpItem Clone() const { return new TGixExpItem(*this); }
    bool Eval(const PGix& Gix, TVec<TItem>& ResItemV, 
		const TPt<TGixMerger<TKey, TItem> >& Merger = _TGixDefMerger::New());

    friend class TPt<TGixExpItem>;
};

template <class TKey, class TItem>
void TGixExpItem<TKey, TItem>::PutAnd(const TPt<TGixExpItem<TKey, TItem> >& _LeftExpItem, 
		const TPt<TGixExpItem<TKey, TItem> >& _RightExpItem) { 

	ExpType = getAnd;
	LeftExpItem = _LeftExpItem;
	RightExpItem = _RightExpItem;
}

template <class TKey, class TItem>
void TGixExpItem<TKey, TItem>::PutOr(const TPt<TGixExpItem<TKey, TItem> >& _LeftExpItem,
		const TPt<TGixExpItem<TKey, TItem> >& _RightExpItem) { 

	ExpType = getOr;
	LeftExpItem = _LeftExpItem;
	RightExpItem = _RightExpItem;
}

template <class TKey, class TItem>
TPt<TGixExpItem<TKey, TItem> > TGixExpItem<TKey, TItem>::NewAndV(
		const TVec<TPt<TGixExpItem<TKey, TItem> > >& ExpItemV) {

	// return empty item if no key is given
	if (ExpItemV.Empty()) { return TGixExpItem<TKey, TItem>::NewEmpty(); }
	// otherwise we start with the first key
	TPt<TGixExpItem<TKey, TItem> > TopExpItem = ExpItemV[0];
	// prepare a queue, which points to the next item (left) to be expanded to tree (and left right)
	TQQueue<TPt<TGixExpItem<TKey, TItem> > > NextExpItemQ;
	// we start with the top
	NextExpItemQ.Push(TopExpItem);
	// add the rest of the items to the expresion tree
	for (int ExpItemN = 1; ExpItemN < ExpItemV.Len(); ExpItemN++) {
		const TPt<TGixExpItem<TKey, TItem> >& RightExpItem = ExpItemV[ExpItemN];
		// which item should we expand
		TPt<TGixExpItem<TKey, TItem> > ExpItem = NextExpItemQ.Top(); NextExpItemQ.Pop();
		// clone the item to be expanded
		TPt<TGixExpItem<TKey, TItem> > LeftExpItem = ExpItem->Clone();
		// and make a new subtree
		ExpItem->PutAnd(LeftExpItem, RightExpItem);
		// update the queue
		NextExpItemQ.Push(ExpItem->LeftExpItem);
		NextExpItemQ.Push(ExpItem->RightExpItem);
	}
	return TopExpItem;
}

template <class TKey, class TItem>
TPt<TGixExpItem<TKey, TItem> > TGixExpItem<TKey, TItem>::NewOrV(
		const TVec<TPt<TGixExpItem<TKey, TItem> > >& ExpItemV) {

	// return empty item if no key is given
	if (ExpItemV.Empty()) { return TGixExpItem<TKey, TItem>::NewEmpty(); }
	// otherwise we start with the first key
	TPt<TGixExpItem<TKey, TItem> > TopExpItem = ExpItemV[0];
	// prepare a queue, which points to the next item (left) to be expanded to tree (and left right)
	TQQueue<TPt<TGixExpItem<TKey, TItem> > > NextExpItemQ;
	// we start with the top
	NextExpItemQ.Push(TopExpItem);
	// add the rest of the items to the expresion tree
	for (int ExpItemN = 1; ExpItemN < ExpItemV.Len(); ExpItemN++) {
		const TPt<TGixExpItem<TKey, TItem> >& RightExpItem = ExpItemV[ExpItemN];
		// which item should we expand
		TPt<TGixExpItem<TKey, TItem> > ExpItem = NextExpItemQ.Top(); NextExpItemQ.Pop();
		// clone the item to be expanded
		TPt<TGixExpItem<TKey, TItem> > LeftExpItem = ExpItem->Clone();
		// and make a new subtree
		ExpItem->PutOr(LeftExpItem, RightExpItem);
		// update the queue
		NextExpItemQ.Push(ExpItem->LeftExpItem);
		NextExpItemQ.Push(ExpItem->RightExpItem);
	}
	return TopExpItem;
}

template <class TKey, class TItem>
TPt<TGixExpItem<TKey, TItem> > TGixExpItem<TKey, TItem>::NewAndV(const TVec<TKey>& KeyV) {
	TVec<TPt<TGixExpItem<TKey, TItem> > > ExpItemV(KeyV.Len(), 0);
	for (int KeyN = 0; KeyN < KeyV.Len(); KeyN++) {
		ExpItemV.Add(TGixExpItem<TKey, TItem>::NewItem(KeyV[KeyN]));
	}
	return NewAndV(ExpItemV);
}

template <class TKey, class TItem>
TPt<TGixExpItem<TKey, TItem> > TGixExpItem<TKey, TItem>::NewOrV(const TVec<TKey>& KeyV) {
	TVec<TPt<TGixExpItem<TKey, TItem> > > ExpItemV(KeyV.Len(), 0);
	for (int KeyN = 0; KeyN < KeyV.Len(); KeyN++) {
		ExpItemV.Add(TGixExpItem<TKey, TItem>::NewItem(KeyV[KeyN]));
	}
	return NewOrV(ExpItemV);
}

template <class TKey, class TItem>
bool TGixExpItem<TKey, TItem>::Eval(const TPt<TGix<TKey, TItem> >& Gix, 
        TVec<TItem>& ResItemV, const TPt<TGixMerger<TKey, TItem> >& Merger) {

    // prepare place for result
    ResItemV.Clr();
    if (ExpType == getOr) {
        EAssert(!LeftExpItem.Empty() && !RightExpItem.Empty());
        TVec<TItem> RightItemV;
        const bool NotLeft = LeftExpItem->Eval(Gix, ResItemV, Merger);
        const bool NotRight = RightExpItem->Eval(Gix, RightItemV, Merger);
        if (NotLeft && NotRight) {
            Merger->Intrs(ResItemV, RightItemV);
        } else if (!NotLeft && !NotRight) { 
            Merger->Union(ResItemV, RightItemV);
        } else {
            TVec<TItem> MinusItemV;
            if (NotLeft) { Merger->Minus(ResItemV, RightItemV, MinusItemV); }
            else { Merger->Minus(RightItemV, ResItemV, MinusItemV); }
            ResItemV = MinusItemV;                       
        }
        return (NotLeft || NotRight);
    } else if (ExpType == getAnd) {
        EAssert(!LeftExpItem.Empty() && !RightExpItem.Empty());
        TVec<TItem> RightItemV;
        const bool NotLeft = LeftExpItem->Eval(Gix, ResItemV, Merger);
        const bool NotRight = RightExpItem->Eval(Gix, RightItemV, Merger);
        if (NotLeft && NotRight) { 
            Merger->Union(ResItemV, RightItemV);
        } else if (!NotLeft && !NotRight) {
            Merger->Intrs(ResItemV, RightItemV);
        } else {
            TVec<TItem> MinusItemV;
            if (NotLeft) { Merger->Minus(RightItemV, ResItemV, MinusItemV); }
            else { Merger->Minus(ResItemV, RightItemV, MinusItemV); }
            ResItemV = MinusItemV;
        }
        return (NotLeft && NotRight);
    } else if (ExpType == getKey) {
        PGixItemSet ItemSet = Gix->GetItemSet(Key);
        if (!ItemSet.Empty()) { 
            ItemSet->Def();
            ItemSet->GetItemV(ResItemV); 
			Merger->Def(ItemSet->GetKey(), ResItemV);
        }
        return false;
    } else if (ExpType == getNot) {
        return !RightExpItem->Eval(Gix, ResItemV, Merger);
    } else if (ExpType == getEmpty) {
        return false; // return nothing
    }
    return true;
}

typedef TGixItemSet<TInt, TInt> TIntGixItemSet;
typedef TPt<TIntGixItemSet> PIntGixItemSet;
typedef TGix<TInt, TIntGixItemSet> TIntGix;
typedef TPt<TIntGix> PIntGix;
typedef TGixExpItem<TInt, TInt> TIntGixExpItem;
typedef TPt<TIntGixExpItem> PIntGixExpItem;

#endif