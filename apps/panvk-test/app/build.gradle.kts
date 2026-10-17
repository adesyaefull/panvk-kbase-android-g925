plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

val panvkSoProp = providers.gradleProperty("panvkSo").orNull
val defaultPanvkPath = File(rootDir.parentFile.parentFile, "build/android-dxint-dist/libvulkan_panfrost.so")
val panvkSoFile = if (panvkSoProp != null) file(panvkSoProp) else defaultPanvkPath
val panvkDrmProp = providers.gradleProperty("panvkDrm").orNull
val panvkDrmFile = panvkDrmProp?.let { file(it) }
val panvkCxxProp = providers.gradleProperty("panvkCxx").orNull
val panvkCxxFile = panvkCxxProp?.let { file(it) }

val checkPanvkSo = tasks.register("checkPanvkSo") {
    inputs.property("panvkSoPath", panvkSoFile.absolutePath)
    outputs.upToDateWhen { panvkSoFile.exists() }
    doLast {
        if (!panvkSoFile.exists()) {
            throw GradleException("Bundled driver panvkSo not found at: ${panvkSoFile.absolutePath}. Specify -PpanvkSo=<path> or ensure default path exists.")
        }
    }
}

val copyPanvkSo = tasks.register<Copy>("copyPanvkSo") {
    dependsOn(checkPanvkSo)
    from(panvkSoFile)
    into(file("build/generated/panvkJni/arm64-v8a"))
    rename { "libvulkan_panfrost.so" }
}

val copyPanvkDrm = tasks.register<Copy>("copyPanvkDrm") {
    if (panvkDrmFile != null) {
        from(panvkDrmFile)
        into(file("build/generated/panvkJni/arm64-v8a"))
        rename { "libdrm.so" }
    }
    onlyIf { panvkDrmFile != null }
}

val copyPanvkCxxRuntime = tasks.register<Copy>("copyPanvkCxxRuntime") {
    dependsOn(copyPanvkDrm)
    dependsOn(copyPanvkSo)
    onlyIf { panvkDrmFile != null }
    val runtime = panvkCxxFile
        ?: if (panvkDrmFile != null) throw GradleException(
            "Bundled libdrm requires libc++.so. Specify -PpanvkCxx=<path-to-libc++_shared.so>."
        ) else null
    if (runtime != null) from(runtime) {
        rename { "libc++.so" }
    }
    into(file("build/generated/panvkJni/arm64-v8a"))
}

tasks.named("preBuild") {
    dependsOn(copyPanvkSo)
    dependsOn(copyPanvkDrm)
    dependsOn(copyPanvkCxxRuntime)
}

android {
    namespace = "dev.zenithblue.panvktest"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "dev.zenithblue.panvktest"
        minSdk = 29
        targetSdk = 36
        versionCode = 8
        versionName = "1.2.4"

        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets.getByName("main") {
        jniLibs.directories.add("build/generated/panvkJni")
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2026.02.01")
    implementation(composeBom)
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
}
