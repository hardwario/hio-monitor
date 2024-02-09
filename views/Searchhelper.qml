import QtQuick

// SearchHelper is a helper object that provides search functionality to a ListView.
// It will continuously search for the pattern in the ListView.
QtObject {
    id: searchEngine

    property ListView view: null
    property var matches: ({})
    property bool isSearching: false
    property string pattern: ""
    property int curRowIndex: 0
    property int curItemInd: -1
    property int prevRow: -1
    property int lastCheckedIndex: 0

    signal noMatch

    onPatternChanged: {
        var firstMatchInd = view.indexOf(pattern)
        if (firstMatchInd === -1) {
            noMatch()
            notify.showError("No match for: " + pattern)
            return
        }

        isSearching = true
        lastCheckedIndex = firstMatchInd
        scrollTo(firstMatchInd)
    }

    function hasMatches() {
        return Object.keys(matches).length !== 0
    }

    function scrollTo(index) {
        view.positionViewAtIndex(index, ListView.Center)
    }

    function findNext() {
        if (!hasMatches())
            return

        curItemInd++

        if (curItemInd >= currentMatch().length) {
            if (curRowIndex >= Object.keys(matches).length - 1) {
                curItemInd--
                return
            }
            curRowIndex++
            curItemInd = 0
        }
        highlightCurrentAndDeselectPrevious()
    }

    function findPrevious() {
        if (!hasMatches())
            return

        curItemInd--

        if (curItemInd < 0) {
            if (curRowIndex <= 0) {
                curItemInd = 0
                return
            }
            curRowIndex--
            curItemInd = currentMatch().length - 1
        }

        highlightCurrentAndDeselectPrevious()
    }

    function highlightCurrentAndDeselectPrevious() {
        const keys = Object.keys(matches)
        const prevItem = view.itemAtIndex(parseInt(keys[prevRow]))
        if (prevItem)
            prevItem.textEditObj.deselect()

        prevRow = curRowIndex
        const curRow = parseInt(keys[curRowIndex])
        scrollTo(curRow)

        const item = view.itemAtIndex(curRow)
        if (!item)
            return

        view.currentIndex = curRow
        const ind = currentMatch()[curItemInd]
        item.textEditObj.select(ind, ind + pattern.length)
    }

    function currentMatch() {
        let keys = Object.keys(matches)
        return matches[keys[curRowIndex]]
    }

    function resetHighlights() {
        isSearching = false
        matches = {}
        pattern = ""
        view.focus = false
        lastCheckedIndex = 0
        curRowIndex = 0
        curItemInd = -1
        prevRow = -1
        for (var i = 0; i < view.count; ++i) {
            const item = view.itemAtIndex(i)
            if (!item)
                continue

            item.reset()
        }
    }

    // meant to be called while isSearching is true
    function searchFor(term) {
        if (term === "")
            return

        pattern = term
        if (!isSearching)
            return

        for (var i = lastCheckedIndex; i < view.count; ++i) {
            const item = view.itemAtIndex(i)
            if (!item)
                continue

            const inds = item.searchFor(pattern)
            if (inds.length > 0) {
                matches[i] = inds
            }

            lastCheckedIndex = i
        }
    }
}
