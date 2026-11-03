# Photo Plus — Product Specification (Foundational Decisions)

> Status: Agreed baseline, recorded before any code is written.
> Every development phase must conform to this document. Changes to it are deliberate decisions, not side effects of implementation.

---

## 1. Application Overview

### 1.1 Application identity

| Property | Decision |
|---|---|
| App name | Photo Plus |
| Platform | Android |
| Minimum Android version | Android 10 (API 29) |
| Minimum RAM target | 3 GB, subject to testing |
| Recommended RAM | 4 GB or more |
| Preferred hardware | 6 GB RAM or more for demanding features |
| Frontend | React Native + TypeScript |
| Android native layer | Kotlin |
| Processing engine | C/C++ |
| Computer vision | OpenCV and suitable vision models |
| AI inference | Compatible on-device inference runtime |
| Primary architecture | Offline-first |
| Subscription | GHS 50 per month |
| Payment platform | Google Play Billing |
| Photoshoot packs | Separate premium-gated purchases |
| Cloud AI APIs | Not required for core functionality |
| Backend | Optional for core editing; online services needed for marketplace distribution and purchase synchronization |

**Note on RAM figures:** These are initial targets, not universal performance guarantees. They will be validated against actual devices, model sizes, image resolutions, and memory usage.

### 1.2 The four pillars

Photo Plus contains four interconnected systems. The **marketplace** supplies additional photography experiences (photoshoot packs) to these systems.

1. **Professional Camera Studio**
   - Camera controls and capture
   - Manual photography
   - Composition assistance
   - Professional camera modes

2. **AI Photography Assistant**
   - Pose detection and matching
   - Live photography guidance
   - Intelligent composition
   - Lighting and image-quality analysis

3. **Photoshoot Replication Studio**
   - Background replacement
   - Reference-based photography
   - Clothing transformation for compatible packs
   - Lighting, shadows, composition, and style reproduction

4. **Professional Editing Studio**
   - Retouching and image enhancement
   - Color grading
   - Advanced masks and effects
   - Watermarks and branding
   - High-quality export

---

## 2. Free and Premium — Rules Every Phase Must Follow

Entitlement rules are established before individual features are built so that free and premium functionality never become mixed as development progresses.

### 2.1 Free plan

Goal: a genuinely useful camera and basic photo editor.

Free users can access:
- Camera capture and basic camera settings
- Basic image cropping and rotation
- Essential exposure, brightness, and contrast adjustments
- Basic filters
- Basic retouching
- Basic focus and portrait effects
- Basic image sharpening
- Watermark styles and positioning
- Photoshoot-pack marketplace browsing
- Pack previews and descriptions
- Premium feature demonstrations

**Free users cannot unlock, download for use, or apply photoshoot packs.** They may browse the marketplace to discover what Photo Plus offers; unlocking a pack requires an active Premium entitlement.

### 2.2 Premium plan — GHS 50/month

Premium users receive:
- All free features
- Advanced camera and editing tools (where supported by the device)
- Advanced background removal and replacement
- Pose guidance and pose matching
- Photoshoot simulation and replication
- Advanced lighting and shadow effects
- Advanced retouching and color grading
- Premium export options
- Custom watermark creation and branding
- Access to eligible photoshoot packs
- Clothing transformation for compatible packs
- New premium tools and capabilities as they are released

### 2.3 Subscription vs. pack ownership

Principle: **the subscription pays for the photography engine; pack purchases pay for additional photography content.** Packs are extensions of the engine, not independent photo editors.

Rules:
1. GHS 50/month unlocks the premium feature suite and the ability to use the pack marketplace.
2. Some packs are **included with Premium**.
3. Other packs require a **separate purchase**, even for Premium subscribers.
4. Once purchased, a pack remains owned by the purchaser according to its stated purchase terms.
5. Purchased pack assets remain available offline after download, subject to the app's entitlement and licensing rules.
6. If a subscription expires, premium tools and subscription-included packs become locked again.
7. Separately purchased packs remain owned, but **applying them still requires an active Premium subscription**.

### 2.4 Entitlement matrix (derived from 2.1–2.3)

| Capability | Free | Premium (active) | Premium expired, pack purchased |
|---|---|---|---|
| Basic camera & basic editing tools | ✅ | ✅ | ✅ |
| Advanced / premium tools | ❌ (demo only) | ✅ | ❌ |
| Browse marketplace, previews, descriptions | ✅ | ✅ | ✅ |
| Unlock / download a pack for use | ❌ | ✅ | — (already owned) |
| Apply a subscription-included pack | ❌ | ✅ | ❌ (locked) |
| Purchase a separately sold pack | ❌ | ✅ | — |
| Own a purchased pack | — | ✅ | ✅ (retained) |
| Apply a purchased pack | — | ✅ | ❌ (requires active Premium) |
| Offline use of downloaded packs | — | ✅ (subject to entitlement checks) | Owned, but not applicable until resubscribed |

---

## 3. Open Questions To Resolve Later

These are not decisions yet; they are recorded so they are not forgotten.

- Offline entitlement verification: how long a cached Premium entitlement is honoured without connectivity (grace period) before tools lock.
- Pack purchase product type in Google Play Billing (non-consumable one-time product) and how ownership syncs across reinstalls/devices.
- Whether a free user can purchase a pack before subscribing (current rules: unlocking requires Premium, so purchase is gated to Premium).
- Exact boundary between "basic" and "advanced" for each tool (retouching, focus/portrait, filters, export).
- Behaviour of already-edited images and saved projects that used premium tools or packs after a subscription lapses.
- Device capability tiers (3 GB / 4 GB / 6 GB+) and which features are disabled or downgraded per tier.
- On-device inference runtime selection and model size budget.
