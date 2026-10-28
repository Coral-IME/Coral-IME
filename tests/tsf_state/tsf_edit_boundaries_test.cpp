#include "pch.h"
#include "editsession.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

GET_DPI_FOR_WINDOW g_fGetDpiForWindow = nullptr;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Interface> class ComObject : public Interface {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(Interface)) return E_NOINTERFACE;
        *output = static_cast<Interface*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = --references_;
        if (!count) delete this;
        return count;
    }
    ULONG references() const { return references_; }
protected:
    virtual ~ComObject() = default;
private:
    ULONG references_ = 1;
};

struct Document {
    std::wstring text = L"nihao";
    HRESULT write_status = S_OK, selection_status = S_OK, end_status = S_OK;
    HRESULT collapse_status = S_OK, clone_status = S_OK;
    bool short_shift = false;
    int writes = 0, selections = 0, ends = 0, clones = 0;
    LONG selection_position = -1;
    std::function<void()> on_write, on_clone;
};

class Range : public ComObject<ITfRange> {
public:
    explicit Range(std::shared_ptr<Document> document) : document_(std::move(document)) {}
    LONG start = 0;
    HRESULT STDMETHODCALLTYPE GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetText(TfEditCookie, DWORD, const WCHAR* text, LONG length) override
    {
        if (FAILED(document_->write_status)) return document_->write_status;
        document_->text.assign(text, length);
        ++document_->writes;
        if (document_->on_write) document_->on_write();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormattedText(TfEditCookie, IDataObject**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InsertEmbedded(TfEditCookie, DWORD, IDataObject*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie, LONG requested, LONG* moved, const TF_HALTCOND* halt) override
    {
        require(halt && halt->aHaltPos == TF_ANCHOR_END && halt->pHaltRange,
            "Cursor movement has no composition end boundary");
        *moved = document_->short_shift ? 0 : requested;
        start += *moved;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsEmpty(TfEditCookie, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie, TfAnchor anchor) override
    {
        if (FAILED(document_->collapse_status)) return document_->collapse_status;
        if (anchor == TF_ANCHOR_END) start = static_cast<LONG>(document_->text.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AdjustForInsert(TfEditCookie, ULONG, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGravity(TfGravity*, TfGravity*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetGravity(TfEditCookie, TfGravity, TfGravity) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(ITfRange** output) override
    {
        *output = nullptr;
        if (FAILED(document_->clone_status)) return document_->clone_status;
        auto* range = new Range(document_);
        range->start = start;
        *output = range;
        ++document_->clones;
        if (document_->on_clone) document_->on_clone();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContext(ITfContext**) override { return E_NOTIMPL; }
private:
    std::shared_ptr<Document> document_;
};

class Composition : public ComObject<ITfComposition> {
public:
    Composition(std::shared_ptr<Document> document, ITfRange* range) : document_(std::move(document)), range_(range) {}
    HRESULT STDMETHODCALLTYPE GetRange(ITfRange** output) override { return range_.CopyTo(output); }
    HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndComposition(TfEditCookie) override { ++document_->ends; return document_->end_status; }
private:
    std::shared_ptr<Document> document_;
    CComPtr<ITfRange> range_;
};

class Context : public ComObject<ITfContext> {
public:
    Context(std::shared_ptr<Document> document, ITfRange* range) : document_(std::move(document)), range_(range) {}
    bool asynchronous = true, empty_selection = false;
    HRESULT request_status = S_OK, session_status = S_OK;
    std::vector<CComPtr<ITfEditSession>> queued;
    HRESULT run_one()
    {
        require(!queued.empty(), "Missing queued edit");
        auto session = queued.front();
        queued.erase(queued.begin());
        return session->DoEditSession(1);
    }
    HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId, ITfEditSession* session, DWORD flags, HRESULT* result) override
    {
        if (FAILED(request_status)) return request_status;
        if (FAILED(session_status)) { *result = session_status; return S_OK; }
        if (asynchronous && !(flags & TF_ES_SYNC)) {
            queued.emplace_back(session); *result = TF_S_ASYNC;
        } else *result = session->DoEditSession(1);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId, BOOL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION* selection, ULONG* fetched) override
    {
        *selection = {};
        *fetched = empty_selection ? 0 : 1;
        if (*fetched) range_.CopyTo(&selection->range);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie, ULONG, const TF_SELECTION* selection) override
    {
        ++document_->selections;
        document_->selection_position = static_cast<Range*>(selection->range)->start;
        return document_->selection_status;
    }
    HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView** output) override { *output = nullptr; return E_FAIL; }
    HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID, ITfProperty** output) override { *output = nullptr; return E_FAIL; }
    HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }
private:
    std::shared_ptr<Document> document_;
    CComPtr<ITfRange> range_;
};

struct Fixture {
    std::shared_ptr<Document> document = std::make_shared<Document>();
    CComPtr<Range> range;
    CComPtr<Composition> composition;
    CComPtr<Context> context;
    std::shared_ptr<CEditMgr> manager = std::make_shared<CEditMgr>(nullptr);
    EditResult result;
    int completions = 0;
    Fixture()
    {
        range.Attach(new Range(document));
        composition.Attach(new Composition(document, range));
        context.Attach(new Context(document, range));
        manager->setContext(CComPtr<ITfContext>(context.p));
        manager->setComposition(CComPtr<ITfComposition>(composition.p));
    }
    ~Fixture() { context->queued.clear(); }
    HRESULT submit(std::function<bool()> preflight = {})
    {
        return manager->insertText(1, L"你好", [this](const EditResult& value) { result = value; ++completions; }, std::move(preflight));
    }
};

void check_completion()
{
    Fixture f;
    require(f.submit() == TF_S_ASYNC && f.completions == 0 && f.document->writes == 0,
        "Accepted request was mistaken for completed text");
    require(FAILED(f.manager->insertText(1, L"duplicate")), "Duplicate pending commit accepted");
    auto session = f.context->queued.front();
    require(f.context->run_one() == S_OK && f.completions == 1 && f.result.text_written && f.result.composition_ended,
        "Actual completion not reported");
    session->DoEditSession(1);
    require(f.document->writes == 1 && f.document->ends == 1 && f.completions == 1,
        "Completed session repeated document side effects");
    require(f.manager->getAllInput() == L"你好", "Written text missing from history");
    Fixture sync; sync.context->asynchronous = false;
    require(sync.submit() == S_OK && sync.completions == 1, "Synchronous completion delivered twice");
    for (bool request_failure : {false, true}) {
        Fixture rejected;
        if (request_failure) rejected.context->request_status = E_ACCESSDENIED;
        else rejected.context->session_status = TF_E_NOLOCK;
        require(FAILED(rejected.submit()) && rejected.completions == 1 && !rejected.result.text_written &&
            !rejected.manager->commit_pending(), "Rejected request lost completion");
    }
    Fixture discarded; discarded.submit(); discarded.context->queued.clear();
    require(discarded.completions == 1 && !discarded.manager->commit_pending(), "Abandoned session stuck pending");
}

void check_write_boundary()
{
    Fixture before; before.document->write_status = E_ACCESSDENIED;
    before.submit(); before.context->run_one();
    require(!before.result.text_written && before.manager->getAllInput().IsEmpty() &&
        before.manager->exsitComposition(), "Failed write consumed composition or learned text");
    before.document->write_status = S_OK;
    before.submit(); before.context->run_one();
    require(before.document->writes == 1, "Retry before writing failed");
    for (bool end_failure : {false, true}) {
        Fixture after;
        if (end_failure) after.document->end_status = E_FAIL;
        else after.document->selection_status = E_FAIL;
        after.submit(); after.context->run_one();
        require(FAILED(after.result.status) && after.result.text_written &&
            after.result.composition_ended == !end_failure && after.document->ends == 1,
            "Post-write failure confused written state or skipped ending");
        if (end_failure) {
            require(FAILED(after.submit()), "Written text was eligible for replay");
            after.manager->updateComposition(1, L"overwrite"); after.context->run_one();
            after.document->end_status = S_OK;
            after.manager->cancel(1); after.context->run_one();
            require(after.document->text == L"你好" && after.document->writes == 1,
                "Cleanup erased or repeated already written text");
        }
    }
    Fixture cancelled;
    cancelled.document->end_status = E_FAIL;
    cancelled.manager->cancel(1); cancelled.context->run_one();
    cancelled.manager->updateComposition(1, L"stale"); cancelled.context->run_one();
    require(cancelled.document->text.empty() && cancelled.document->writes == 1,
        "Failed cancellation cleanup restored stale preedit");
}

void check_stale_edits()
{
    for (int scenario = 0; scenario < 6; ++scenario) {
        Fixture f;
        bool valid = true;
        f.submit([&] { return valid; });
        if (scenario == 0) { f.manager->set_edit_focus(false); f.manager->set_edit_focus(true); }
        if (scenario == 1) f.manager->retire_edits();
        if (scenario == 2) f.manager->set_focus_validator([](ITfContext*) { return false; });
        if (scenario == 3) valid = false;
        if (scenario == 4) f.manager->clearTerminatedComposition(f.composition);
        if (scenario == 5) {
            CComPtr<Composition> replacement; replacement.Attach(new Composition(f.document, f.range));
            f.manager->setComposition(CComPtr<ITfComposition>(replacement.p));
        }
        require(f.context->run_one() == E_ABORT && f.completions == 1 && f.document->writes == 0 &&
            f.document->selections == 0 && f.document->ends == 0, "Stale callback touched document");
    }
    Fixture reentrant;
    reentrant.document->on_write = [&] { reentrant.manager->set_edit_focus(false); };
    reentrant.submit(); reentrant.context->run_one();
    require(reentrant.result.text_written && reentrant.document->selections == 0,
        "SetText reentrancy moved caret after focus loss");

    Fixture renewed;
    renewed.submit();
    renewed.manager->set_edit_focus(false); renewed.manager->set_edit_focus(true);
    renewed.submit();
    require(renewed.context->run_one() == E_ABORT && renewed.manager->commit_pending(),
        "Old completion cleared the new commit's pending state");
    require(renewed.context->run_one() == S_OK && renewed.document->writes == 1,
        "Refocused composition could not accept a fresh commit");

    Fixture closing;
    closing.submit();
    require(closing.manager->cancelSynchronously(1) == S_OK, "Synchronous close cleanup failed");
    require(closing.context->run_one() == E_ABORT && closing.document->text.empty() &&
        closing.manager->getAllInput().IsEmpty(), "Pending commit wrote after close cleanup");

    Fixture pending_start;
    pending_start.manager->setComposition({});
    require(pending_start.manager->start(1, pending_start.context, nullptr) == TF_S_ASYNC &&
        pending_start.manager->start(1, pending_start.context, nullptr) == TF_S_ASYNC &&
        pending_start.context->queued.size() == 1, "Pending start was submitted twice");
    pending_start.manager->cancel(1);
    require(pending_start.context->run_one() == E_ABORT && !pending_start.manager->start_pending(),
        "Cancelled pending start remained executable");
}

void check_cursor_and_geometry()
{
    for (int scenario = 0; scenario < 5; ++scenario) {
        Fixture f;
        if (scenario == 1) f.document->short_shift = true;
        if (scenario == 2) f.document->collapse_status = E_FAIL;
        if (scenario == 3) f.document->clone_status = E_FAIL;
        if (scenario == 4) f.document->on_clone = [&] { f.manager->set_edit_focus(false); };
        f.manager->setCursorPos(1, 2);
        const HRESULT status = f.context->run_one();
        require(f.range->start == 0, "Caret operation modified the source composition range");
        if (scenario == 0) require(status == S_OK && f.document->selection_position == 2, "Valid cursor rejected");
        else require(FAILED(status) && f.document->selections == 0, "Partial or stale cursor changed selection");
    }
    Fixture f; f.manager->setCursorPos(1, -1);
    require(FAILED(f.context->run_one()) && f.document->clones == 0, "Negative cursor accepted");
    const ULONG references = f.range->references();
    CRect bounds(1, 2, 3, 4);
    require(FAILED(f.manager->getTextBound(1, bounds)) && f.range->references() == references &&
        bounds == CRect(1, 2, 3, 4), "Geometry failure leaked range or published invalid bounds");
    f.context->empty_selection = true;
    require(FAILED(f.manager->getTextBound(1, bounds)), "Empty selection accepted as geometry");
}
}

int main()
{
    try {
        check_completion();
        check_write_boundary();
        check_stale_edits();
        check_cursor_and_geometry();
        std::cout << "TSF edit completion, cursor and focus boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
