package com.hypnodroid.blorbrelay

import android.app.Application
import com.hypnodroid.blorbrelay.sync.SyncEngine

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        Notify.channels(this)
        SyncEngine.init(this)
    }
}
