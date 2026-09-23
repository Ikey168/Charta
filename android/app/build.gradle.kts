plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val releaseTaskRequested = gradle.startParameter.taskNames.any { it.contains("Release", ignoreCase = true) }
val releaseKeystore = System.getenv("DOODLEBOUND_KEYSTORE")
val releaseAlias = System.getenv("DOODLEBOUND_KEY_ALIAS")
val releaseStorePassword = System.getenv("DOODLEBOUND_STORE_PASSWORD")
val releaseKeyPassword = System.getenv("DOODLEBOUND_KEY_PASSWORD")
val releaseCredentialsPresent = listOf(releaseKeystore, releaseAlias, releaseStorePassword, releaseKeyPassword)
    .all { !it.isNullOrBlank() }
if (releaseTaskRequested && !releaseCredentialsPresent) {
    throw GradleException("Release signing requires DOODLEBOUND_KEYSTORE, DOODLEBOUND_KEY_ALIAS, " +
        "DOODLEBOUND_STORE_PASSWORD and DOODLEBOUND_KEY_PASSWORD")
}

android {
    namespace = "com.ikore.doodlebound"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.ikore.doodlebound"
        minSdk = 29
        targetSdk = 36
        versionCode = System.getenv("DOODLEBOUND_VERSION_CODE")?.toIntOrNull() ?: 1
        versionName = System.getenv("DOODLEBOUND_VERSION_NAME") ?: "0.1.0-demo"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake { cppFlags += listOf("-std=c++17", "-fexceptions", "-frtti") }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures { prefab = true }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    if (releaseCredentialsPresent) {
        signingConfigs {
            create("demoRelease") {
                storeFile = file(releaseKeystore!!)
                storePassword = releaseStorePassword
                keyAlias = releaseAlias
                keyPassword = releaseKeyPassword
            }
        }
        buildTypes { getByName("release") { signingConfig = signingConfigs.getByName("demoRelease") } }
    }

    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    implementation("androidx.games:games-activity:4.4.2")
    // GameActivity's published POM omits its Java supertypes; declare them here.
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.core:core:1.17.0")
    androidTestImplementation("androidx.test:core:1.6.1")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
}
