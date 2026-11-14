import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import {
  cropFor,
  horizonInFrame,
  lookFor,
  PLATES,
  suggestPlacement,
  type Background,
  type Crop,
  type FrameShape,
  type Placement,
  type SceneLook,
} from '../../services/studio/scene';
import {
  DEFAULT_EDGE,
  StudioError,
  type CutoutResult,
  type EdgeSettings,
  type SceneResult,
  type Session,
  type Stroke,
  type StudioService,
} from '../../services/studio/StudioService';

export type Step = 'pick' | 'cutout' | 'scene';

/**
 * Runs `job` for the newest request only: while one runs, later requests
 * wait and only the last of them runs next (sliders and strokes come fast;
 * the phone shows the latest).
 */
function useLatest<A>(job: (arg: A) => Promise<void>) {
  const running = useRef(false);
  const next = useRef<{ arg: A } | null>(null);
  const jobRef = useRef(job);
  useEffect(() => {
    jobRef.current = job;
  }, [job]);
  return useCallback((arg: A) => {
    next.current = { arg };
    if (running.current) return;
    running.current = true;
    void (async () => {
      while (next.current) {
        const { arg: a } = next.current;
        next.current = null;
        try {
          await jobRef.current(a);
        } catch {
          // Reported by the job itself.
        }
      }
      running.current = false;
    })();
  }, []);
}

const NO_PERSON = 'Nobody was found in this photo. Choose a photo with a person in it, or tap the person.';

export function useSceneSwap(studio: StudioService) {
  const [step, setStep] = useState<Step>('pick');
  const [session, setSession] = useState<Session | null>(null);
  const [sourcePath, setSourcePath] = useState<string | null>(null);
  const [cut, setCut] = useState<CutoutResult | null>(null);
  const [strokes, setStrokes] = useState<Stroke[]>([]);
  const [redo, setRedo] = useState<Stroke[]>([]);
  const [edge, setEdge] = useState<EdgeSettings>(DEFAULT_EDGE);
  const [background, setBackground] = useState<Background>({ kind: 'plate', plate: PLATES[0] });
  const [shape, setShape] = useState<FrameShape>('portrait');
  const [placement, setPlacement] = useState<Placement | null>(null);
  const [lookOverride, setLookOverride] = useState<Partial<SceneLook>>({});
  const [follow, setFollow] = useState(true);
  const [scene, setScene] = useState<SceneResult | null>(null);
  const [empty, setEmpty] = useState<SceneResult | null>(null);
  const [busy, setBusy] = useState<'start' | 'cut' | 'render' | 'save' | null>(null);
  const [error, setError] = useState<string | null>(null);

  const report = cut?.report ?? session?.report ?? null;
  const photoAspect = session ? session.width / Math.max(1, session.height) : 1;

  // The frame: the background's own shape or a crop of it, kept on the person.
  const bgSize = useMemo(() => {
    if (background.kind === 'plate') return { w: background.plate.width, h: background.plate.height };
    return { w: session?.width ?? 3, h: session?.height ?? 4 };
  }, [background, session]);
  const crop: Crop = useMemo(
    () => (background.kind === 'plate' ? cropFor(bgSize.w, bgSize.h, shape, placement?.x ?? 0.5) : { x: 0, y: 0, w: 1, h: 1 }),
    // The crop follows the person only when the background or shape changes, not on every drag.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [background, shape, bgSize],
  );
  const frameAspect = useMemo(() => {
    if (background.kind !== 'plate') {
      const aspects: Record<FrameShape, number> = { scene: photoAspect, portrait: 0.8, story: 9 / 16, square: 1 };
      return aspects[shape];
    }
    return (bgSize.w * crop.w) / Math.max(1, bgSize.h * crop.h);
  }, [background, shape, bgSize, crop, photoAspect]);
  const baseLook = useMemo(() => lookFor(background, crop), [background, crop]);
  const look: SceneLook = useMemo(() => ({ ...baseLook, ...lookOverride }), [baseLook, lookOverride]);
  const horizon = look.horizon;

  // ---- the cut-out
  const refine = useLatest(async ({ id, s, e }: { id: string; s: Stroke[]; e: EdgeSettings }) => {
    setBusy('cut');
    try {
      setCut(await studio.refine(id, s, e));
      setError(null);
    } catch (err) {
      setError(err instanceof StudioError ? 'That correction couldn’t be applied. Please try again.' : String(err));
    } finally {
      setBusy(b => (b === 'cut' ? null : b));
    }
  });

  const start = useCallback(
    async (path: string, taps: { x: number; y: number }[] = []) => {
      setBusy('start');
      setError(null);
      try {
        if (session) studio.end(session.sessionId);
        const s = await studio.start(path, taps);
        setSourcePath(path);
        setSession(s);
        setCut(s);
        setStrokes([]);
        setRedo([]);
        setEdge(DEFAULT_EDGE);
        setPlacement(null);
        setScene(null);
        setEmpty(null);
        if (!s.report.found) setError(NO_PERSON);
        setStep('cutout');
      } catch (err) {
        setError(err instanceof StudioError && err.code === 'NO_PERSON' ? NO_PERSON : 'That photo couldn’t be opened. Please try another.');
      } finally {
        setBusy(null);
      }
    },
    [session, studio],
  );

  /** A group photo: the person tapped is the one to keep. */
  const chooseWho = useCallback(
    (p: { x: number; y: number }) => {
      if (sourcePath) void start(sourcePath, [p]);
    },
    [sourcePath, start],
  );

  const addStroke = useCallback(
    (s: Stroke) => {
      if (!session) return;
      const next = [...strokes, s];
      setStrokes(next);
      setRedo([]);
      refine({ id: session.sessionId, s: next, e: edge });
    },
    [session, strokes, edge, refine],
  );
  const undo = useCallback(() => {
    if (!session || strokes.length === 0) return;
    const last = strokes[strokes.length - 1];
    const next = strokes.slice(0, -1);
    setStrokes(next);
    setRedo(r => [...r, last]);
    refine({ id: session.sessionId, s: next, e: edge });
  }, [session, strokes, edge, refine]);
  const redoStroke = useCallback(() => {
    if (!session || redo.length === 0) return;
    const s = redo[redo.length - 1];
    const next = [...strokes, s];
    setStrokes(next);
    setRedo(r => r.slice(0, -1));
    refine({ id: session.sessionId, s: next, e: edge });
  }, [session, strokes, redo, edge, refine]);
  const changeEdge = useCallback(
    (e: Partial<EdgeSettings>) => {
      if (!session) return;
      const next = { ...edge, ...e };
      setEdge(next);
      refine({ id: session.sessionId, s: strokes, e: next });
    },
    [session, strokes, edge, refine],
  );
  const resetCutout = useCallback(() => {
    if (!session) return;
    setStrokes([]);
    setRedo([]);
    setEdge(DEFAULT_EDGE);
    refine({ id: session.sessionId, s: [], e: DEFAULT_EDGE });
  }, [session, refine]);

  // ---- the scene
  const defaultPlacement = useCallback(
    (bg: Background, sh: FrameShape): Placement | null => {
      if (!report) return null;
      const bs = bg.kind === 'plate' ? { w: bg.plate.width, h: bg.plate.height } : bgSize;
      const c = bg.kind === 'plate' ? cropFor(bs.w, bs.h, sh, 0.5) : { x: 0, y: 0, w: 1, h: 1 };
      const aspect = bg.kind === 'plate' ? (bs.w * c.w) / Math.max(1, bs.h * c.h) : frameAspect;
      const h = bg.kind === 'plate' ? horizonInFrame(bg.plate.horizon, c) : 0.55;
      const floor = bg.kind === 'plate' ? Math.min(0.97, (bg.plate.floor - c.y) / c.h) : 0.94;
      return suggestPlacement(
        { box: report.box, cutBottom: report.cutBottom, cutTop: report.cutTop, cutSides: report.cutLeft || report.cutRight, aspect: photoAspect },
        h,
        floor,
        aspect,
      );
    },
    [report, bgSize, frameAspect, photoAspect],
  );

  const render = useLatest(async ({ id, bg, c, p, l, aspect }: { id: string; bg: Background; c: Crop; p: Placement; l: SceneLook; aspect: number }) => {
    setBusy('render');
    try {
      const [withPerson, without] = await Promise.all([
        studio.render(id, { background: bg, crop: c, placement: p, look: l, aspect }, 'preview'),
        bg.kind === 'transparent' ? Promise.resolve(null) : studio.render(id, { background: bg, crop: c, placement: p, look: l, aspect }, 'preview', true),
      ]);
      setScene(withPerson);
      setEmpty(without);
      setError(null);
    } catch {
      setError('The scene couldn’t be drawn. Please try again.');
    } finally {
      setBusy(b => (b === 'render' ? null : b));
    }
  });

  useEffect(() => {
    if (step !== 'scene' || !session || !placement) return;
    render({ id: session.sessionId, bg: background, c: crop, p: placement, l: look, aspect: frameAspect });
  }, [step, session, background, crop, placement, look, frameAspect, render, cut]);

  const goToScene = useCallback(() => {
    setStep('scene');
    setPlacement(p => p ?? defaultPlacement(background, shape));
  }, [background, shape, defaultPlacement]);

  const chooseBackground = useCallback(
    (bg: Background) => {
      setBackground(bg);
      setLookOverride({});
      setPlacement(defaultPlacement(bg, shape));
    },
    [shape, defaultPlacement],
  );
  const chooseShape = useCallback(
    (sh: FrameShape) => {
      setShape(sh);
      setPlacement(defaultPlacement(background, sh));
    },
    [background, defaultPlacement],
  );
  const resetPlacement = useCallback(() => setPlacement(defaultPlacement(background, shape)), [background, shape, defaultPlacement]);

  // ---- saving
  const save = useCallback(async (): Promise<SceneResult | null> => {
    if (!session || !placement) return null;
    setBusy('save');
    setError(null);
    try {
      return await studio.render(session.sessionId, { background, crop, placement, look, aspect: frameAspect }, 'full');
    } catch (err) {
      if (err instanceof StudioError && err.code === 'PREMIUM_REQUIRED') throw err;
      setError('Your photo couldn’t be saved. Please try again.');
      return null;
    } finally {
      setBusy(null);
    }
  }, [session, placement, background, crop, look, frameAspect, studio]);

  const saveCutout = useCallback(async (): Promise<SceneResult | null> => {
    if (!session) return null;
    setBusy('save');
    setError(null);
    try {
      return await studio.render(
        session.sessionId,
        { background: { kind: 'transparent' }, crop: { x: 0, y: 0, w: 1, h: 1 }, placement: placement ?? { x: 0.5, y: 1, height: 1, flip: false, tilt: 0, angle: 0 }, look },
        'full',
      );
    } catch (err) {
      if (err instanceof StudioError && err.code === 'PREMIUM_REQUIRED') throw err;
      setError('Your cut-out couldn’t be saved. Please try again.');
      return null;
    } finally {
      setBusy(null);
    }
  }, [session, placement, look, studio]);

  useEffect(() => () => {
    if (session) studio.end(session.sessionId);
  }, [session, studio]);

  return {
    step,
    setStep,
    session,
    cut,
    report,
    photoAspect,
    strokes,
    canRedo: redo.length > 0,
    edge,
    background,
    shape,
    crop,
    frameAspect,
    placement,
    setPlacement,
    look,
    setLook: (l: Partial<SceneLook>) => setLookOverride(o => ({ ...o, ...l })),
    follow,
    setFollow,
    horizon,
    scene,
    empty,
    busy,
    error,
    setError,
    start,
    chooseWho,
    addStroke,
    undo,
    redo: redoStroke,
    changeEdge,
    resetCutout,
    goToScene,
    chooseBackground,
    chooseShape,
    resetPlacement,
    save,
    saveCutout,
  };
}

export type SceneSwap = ReturnType<typeof useSceneSwap>;
